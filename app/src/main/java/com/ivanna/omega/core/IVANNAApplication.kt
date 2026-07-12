package com.ivanna.omega.core

import android.app.Application
import android.util.Log
import com.ivanna.omega.audio.IvannaGlobalEffectManager
import com.ivanna.omega.dsp.DSPBridge
import com.ivanna.omega.magisk.OmegaDaemon
import com.ivanna.omega.magisk.OmegaEngineBridge
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.channels.Channel
import android.content.IntentFilter
import android.media.audiofx.AudioEffect
import com.ivanna.omega.audio.AudioSessionReceiver
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

/**
 * IVANNAApplication — Punto de entrada de la aplicación.
 *
 * FIXES DE CONECTIVIDAD:
 *   1. Expone globalEffectManager como propiedad pública para que
 *      AudioSessionReceiver pueda acceder a él via applicationContext.
 *   2. Inicializa globalEffectManager ANTES del OmegaDaemon para que
 *      las primeras sesiones de audio ya tengan efectos disponibles.
 *   3. isInitialized es Thread-safe (@Volatile).
 *   4. onTerminate() libera globalEffectManager correctamente.
 */
class IVANNAApplication : Application() {

    companion object {
        private const val TAG = "IVANNAApplication"
        val appScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

        /**
         * Canal CONFLATED: sustituye appScope.launch x N en pushToNative().
         * Con CONFLATED solo el ultimo valor de parametros DSP llega al socket;
         * los intermedios se descartan. Elimina el crash por OOM (coroutines
         * bloqueadas en connect() CONNECT_TIMEOUT_MS=2000ms a 60fps = miles de
         * threads IO bloqueados despues de minutos de uso).
         */
        val pfParamChannel = Channel<FloatArray>(Channel.CONFLATED)
        val omegaBridge = OmegaEngineBridge()

        @Volatile
        var isInitialized = false
            private set
    }

    // FIX: expuesto como propiedad de instancia (no companion) para que
    // AudioSessionReceiver lo acceda via (context.applicationContext as IVANNAApplication)
    val globalEffectManager = IvannaGlobalEffectManager()

    override fun onCreate() {
        super.onCreate()
        Log.d(TAG, "=== IVANNA DSP Application iniciada ===")

        // FIX (crash): consumer unico del canal DSP.
        // Un solo socket write activo a la vez; CONFLATED descarta los intermedios.
        appScope.launch {
            for (params in pfParamChannel) {
                runCatching {
                    omegaBridge.setPFParams(
                        params[0], params[1], params[2],
                        params[3], params[4], params[5],
                        params[6], params[7],
                        params[8], params[9], params[10], params[11], params[12]
                    )
                }
            }
        }

        // Conectar DSPState con GlobalEffectManager ANTES de que la UI cargue.
        // Sin esto, pushToNative() nunca llama adjustLiveParams() y los sliders
        // de EQ/Width/Exciter/Comp no afectan Spotify/YouTube/ninguna app externa.
        com.ivanna.omega.dsp.DSPState.globalEffectManager = globalEffectManager

        // FIX (controles Android 13+): registrar AudioSessionReceiver dinámicamente
        // con RECEIVER_NOT_EXPORTED ademas del Manifest, ya que en API33+
        // el sistema puede no enviar broadcasts implícitos a receivers solo de Manifest.
        runCatching {
            val filter = IntentFilter().apply {
                addAction(AudioEffect.ACTION_OPEN_AUDIO_EFFECT_CONTROL_SESSION)
                addAction(AudioEffect.ACTION_CLOSE_AUDIO_EFFECT_CONTROL_SESSION)
            }
            val flags = if (android.os.Build.VERSION.SDK_INT >= 33)
                android.content.Context.RECEIVER_NOT_EXPORTED else 0
            registerReceiver(AudioSessionReceiver(), filter, flags)
        }.onFailure { Log.w("IVANNAApp", "AudioSessionReceiver dyn reg failed: ${it.message}") }

        // FIX: OmegaEngine se inicializa con el Context ANTES del scope IO
        OmegaEngine.init(this)

        // FIX (rehabilitación — Prioridad 1.5 más alta de
        // IVANNA_ARCHITECTURE_DECISION_REPORT.md): AudioRouteManager nunca
        // se instanciaba en todo el repo pese a que su destino
        // (control_set_route_profile() en audio_control_plane.hpp) ya
        // estaba confirmado vivo y consumido de verdad por
        // control_apply_frame() (route_bass_boost alimenta f.low,
        // route_dialog_boost alimenta el EQ combinado, route_widener_mult
        // el ancho estéreo — verificado línea por línea antes de conectar
        // esto). Sin el manager arrancado, la compensación por ruta de
        // salida (Bluetooth SBC/AAC lossy, rolloff de graves en AUX) nunca
        // se activaba, aunque el motor que la aplica funcionaba bien.
        //
        // DEBE ser síncrono en el hilo principal, NO dentro de
        // appScope.launch (Dispatchers.IO): AudioManager.
        // registerAudioDeviceCallback(callback, null) usa el Looper del
        // hilo que llama para entregar los callbacks — un hilo del pool de
        // IO no tiene Looper preparado, lo que puede fallar en tiempo de
        // ejecución. Application.onCreate() corre garantizado en el hilo
        // principal (con Looper), por eso va aquí y no más abajo.
        com.ivanna.omega.audio.AudioRouteManager.start(this)

        // FIX (carrera): esto DEBE ser síncrono, no ir dentro de appScope.launch.
        // MainActivity.onCreate() llama a IvannaNativeLib.nativeStartEvoThread()
        // directamente (si evo_enabled) en el hilo principal, sin esperar a
        // Application.appScope (Dispatchers.IO) — si el path se fijara ahí,
        // llegaría tarde la mayoría de las veces y evo_initialize_population()
        // correría con g_savePath vacío (persistencia deshabilitada de facto).
        if (IvannaNativeLib.isLoaded) {
            IvannaNativeLib.nativeSetEvoSavePath(
                "${filesDir.absolutePath}/evo_population.bin"
            )
        }

        appScope.launch {
            try {
                // 1. DSP nativo
                DSPBridge.init(sampleRate = 48000)
                Log.d(TAG, "✅ DSPBridge listo — 48000 Hz")

                // 2. Daemon Magisk (puede fallar sin root — no es fatal)
                val daemonOk = OmegaDaemon.start()
                Log.d(TAG, if (daemonOk) "✅ OmegaDaemon iniciado"
                           else          "⚠️ OmegaDaemon no disponible (modo no-root activo)")

                // 3. Socket bridge al daemon (esperar 300ms a que inicie)
                delay(300)
                omegaBridge.connect()
                Log.d(TAG, "✅ OmegaEngineBridge conectando en background")

                isInitialized = true
                Log.i(TAG, "✅ IVANNA-OMEGA-SUPREME lista")
            } catch (e: UnsatisfiedLinkError) {
                Log.e(TAG, "❌ Librería nativa no disponible: ${e.message}")
            } catch (e: Exception) {
                Log.e(TAG, "❌ Error de inicialización: ${e.message}")
            }
        }

        // Sync de perfiles en la nube: no-op seguro si CloudSyncManager no
        // está configurado todavía (ver CloudSyncManager.kt). Separado del
        // bloque de arriba a propósito — no debe bloquear ni afectar el
        // arranque del motor DSP si falla o tarda (red lenta, etc.).
        appScope.launch {
            try {
                CloudSyncManager.syncDown(this@IVANNAApplication, UserProfileManager(this@IVANNAApplication))
            } catch (e: Exception) {
                Log.w(TAG, "syncDown en arranque falló (no crítico): ${e.message}")
            }
        }
    }

    override fun onTerminate() {
        // Best-effort: onTerminate() no está garantizado en dispositivos reales,
        // pero el autosave periódico en evolveGeneration() ya cubre el caso de
        // que el proceso muera sin pasar por aquí.
        if (IvannaNativeLib.isLoaded) {
            try {
                IvannaNativeLib.nativeSaveEvoState()
            } catch (e: Exception) {
                Log.e(TAG, "⚠️ No se pudo guardar evo state en onTerminate: ${e.message}")
            }
        }
        globalEffectManager.releaseAll()
        omegaBridge.disconnect()
        OmegaDaemon.stop()
        com.ivanna.omega.audio.AudioRouteManager.stop()
        super.onTerminate()
    }
}
