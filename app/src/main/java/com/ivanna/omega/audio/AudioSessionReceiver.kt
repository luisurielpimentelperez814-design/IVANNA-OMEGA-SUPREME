package com.ivanna.omega.audio

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.media.audiofx.AudioEffect
import android.util.Log

/**
 * AudioSessionReceiver — Receptor de sesiones de audio globales.
 *
 * FIX: Este receptor faltaba completamente. Sin el, IvannaGlobalEffectManager
 * nunca recibia las sesiones de otras apps (Spotify, YouTube, etc.) y los
 * efectos globales no se aplicaban.
 *
 * Funcionamiento (identico a Wavelet EQ y Poweramp Equalizer):
 *   1. Android emite OPEN_AUDIO_EFFECT_CONTROL_SESSION cuando cualquier app
 *      abre una sesion de audio.
 *   2. Este receptor captura el audioSessionId y el packageName de la app.
 *   3. Delega a IvannaGlobalEffectManager para aplicar el perfil activo.
 *   4. Cuando la sesion se cierra, libera los efectos sin memory leak.
 *
 * ENDURECIDO:
 *   - sessionId invalido (0 o negativo — algunos OEM emiten broadcasts vacios
 *     al arrancar por spec de AOSP) descartado con log de rastro, no
 *     propagado al GlobalEffectManager (que hoy no valida el id y creaba
 *     efectos huerfanos con handle 0).
 *   - Comprobacion segura de que el applicationContext es realmente
 *     IVANNAApplication: en escenarios de multi-proceso (isolatedProcess
 *     de un Service, tests instrumentados) el `applicationContext` puede
 *     ser una instancia distinta o mock, y el cast implicito con `is`
 *     tolera eso sin crashear.
 *   - EXTRA_PACKAGE_NAME expuesto como constante local (la clave
 *     "android.media.extra.PACKAGE_NAME" es la misma en todas las versiones
 *     de Android — evita ir a buscarla como magic string).
 *   - Accion desconocida ya no cae al else silencioso: se registra a nivel
 *     verbose para no perder rastro si un OEM emite variantes propietarias.
 *   - v2.5.374 — CAUSA RAÍZ DEL RECEIVER "ROTO" (Moto G85, 2026-11-09):
 *     el registro DINÁMICO en IVANNAApplication usaba RECEIVER_NOT_EXPORTED
 *     (API 33+). Ese flag solo deja recibir broadcasts de la propia app;
 *     OPEN_AUDIO_EFFECT_CONTROL_SESSION lo emite system_server (otro UID),
 *     así que el receiver dinámico NUNCA podía disparar. El receiver de
 *     manifest (exported=true) es el único que lo recibe cuando el proceso
 *     está vivo, y tras un force-stop/reboot la app solo vuelve a ver
 *     sesiones creadas DESPUÉS de arrancar. Por eso el manager ahora
 *     también sondea sesiones activas (ver IvannaGlobalEffectManager
 *     .scanActivePlaybackSessions) y el registro dinámico usa
 *     RECEIVER_EXPORTED. Además, un broadcast sin packageName ya no
 *     descarta la sesión (fail-open): el sessionId es la pieza crítica.
 */
class AudioSessionReceiver : BroadcastReceiver() {

    companion object {
        private const val TAG = "AudioSessionReceiver"

        // Extra estable definida por AOSP para el paquete emisor del broadcast.
        // Referenciada aqui para no depender de una magic string por todo el codigo.
        private const val EXTRA_PACKAGE_NAME = "android.media.extra.PACKAGE_NAME"
    }

    override fun onReceive(context: Context, intent: Intent) {
        val sessionId = intent.getIntExtra(AudioEffect.EXTRA_AUDIO_SESSION, 0)
        val packageName = intent.getStringExtra(EXTRA_PACKAGE_NAME)

        // v2.5.374 — RASTRO DE ENTREGA: antes, un broadcast recibido pero
        // descartado solo dejaba un Log.v/Log.w que el grep de diagnóstico
        // ("Nueva sesion|...") no mostraba. Si el receiver deja de detectar
        // sesiones, la primera pregunta es si el broadcast llega siquiera.
        // Se registra TODA entrega a nivel INFO para que un logcat filtrado
        // por el tag del receiver muestre si el sistema emite el broadcast.
        Log.i(TAG, "Broadcast recibido: action=${intent.action} sessionId=$sessionId pkg=$packageName")

        // Descarta broadcasts vacios: algunos OEM emiten OPEN con sessionId=0
        // al inicializar el AudioService al arrancar el sistema, y otros
        // (Xiaomi/Vivo en API33+) reenvian el ACTION_CLOSE con id negativo
        // cuando el pool de sesiones se resetea. Ambos casos son ruido, no
        // sesiones reales de apps de terceros.
        if (sessionId <= 0) {
            Log.v(TAG, "Broadcast ignorado: sessionId invalido=$sessionId action=${intent.action} pkg=$packageName")
            return
        }

        val app = context.applicationContext as? com.ivanna.omega.core.IVANNAApplication
        if (app == null) {
            // Test harness / isolated process / cache stale del receiver
            // en escenarios de reinicio de proceso: no se puede tocar el
            // manager global sin arriesgar un NPE. Rastro claro para
            // diagnosticar sin tumbar el broadcast.
            Log.w(TAG, "applicationContext no es IVANNAApplication (${context.applicationContext.javaClass.name}) — sesion $sessionId ignorada")
            return
        }

        // El reproductor propio de IVANNA ya ejecuta DSPBridge + su cadena espacial
        // antes de escribir en AudioTrack. No adjuntar omega_effect ni los efectos
        // globales a esa misma sesión: produciría doble procesamiento.
        if (packageName == context.packageName) {
            Log.v(TAG, "Sesión propia de IVANNA excluida del manager global: id=$sessionId")
            return
        }
        // v2.5.374 — FAIL-OPEN ante packageName ausente: el broadcast de
        // AOSP incluye EXTRA_PACKAGE_NAME, pero algunos firmwares/OEM lo
        // omiten. Antes, un broadcast sin paquete se descartaba y la sesión
        // quedaba sin efectos para siempre (el receiver parecía "roto" sin
        // dejar rastro útil). El sessionId es la pieza crítica; el paquete
        // solo alimenta la clasificación video/música (isVideoStreamingPackage
        // tolera null → no-video). Se abre la sesión con paquete desconocido
        // y se deja constancia, en vez de perderla.
        if (packageName.isNullOrBlank()) {
            Log.w(TAG, "Sesión $sessionId sin packageName en el broadcast — se abre como origen desconocido (no-video)")
        }

        when (intent.action) {
            AudioEffect.ACTION_OPEN_AUDIO_EFFECT_CONTROL_SESSION -> {
                Log.i(TAG, "Nueva sesion de audio: id=$sessionId pkg=$packageName")
                runCatching { app.globalEffectManager.openSession(sessionId, packageName) }
                    .onFailure { Log.w(TAG, "openSession($sessionId): ${it.message}") }
            }
            AudioEffect.ACTION_CLOSE_AUDIO_EFFECT_CONTROL_SESSION -> {
                Log.i(TAG, "Cerrando sesion de audio: id=$sessionId pkg=$packageName")
                runCatching { app.globalEffectManager.closeSession(sessionId) }
                    .onFailure { Log.w(TAG, "closeSession($sessionId): ${it.message}") }
            }
            else -> Log.v(TAG, "Accion desconocida ignorada: ${intent.action}")
        }
    }
}
