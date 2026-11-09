package com.ivanna.omega.audio

import android.media.audiofx.AudioEffect
import android.util.Log
import com.ivanna.omega.dsp.DSPBridge
import java.util.UUID
import java.util.concurrent.atomic.AtomicReference

/**
 * RouteArbiter -- vigilancia dinamica de conflicto DSP entre Ruta A
 * (PlaybackCaptureService / CaptureEngine, in-process, MediaProjection
 * loopback -> AudioTrack) y Ruta B (omega_effect.cpp, InsertEffect de
 * AudioFlinger cargado via modulo Magisk).
 *
 * Esta clase NO procesa audio, NO toca SCHED_FIFO/afinidad CPU/el daemon
 * IPC/SHM/el pipeline DSP en C++. Unicamente consulta la API publica
 * AudioEffect.queryEffects() (sin permisos nuevos -- misma llamada que ya
 * usaba el guard original de CaptureEngine.start()) para saber si el
 * efecto de sistema omega_effect esta registrado, y publica un estado
 * que PlaybackCaptureService usa para decidir si Ruta A puede seguir
 * corriendo.
 *
 * Debe invocarse siempre desde un hilo de control (ej. el retryHandler
 * de PlaybackCaptureService) -- NUNCA desde el hilo de audio
 * URGENT_AUDIO ni desde processingLoop.
 */
object RouteArbiter {

    enum class RouteState {
        // omega_effect (Ruta B) activo en AudioFlinger y Ruta A detenida.
        // Estado normal cuando el modulo Magisk esta cargado.
        SYSTEM_WIDE_ONLY,

        // omega_effect NO activo, Ruta A corriendo sola. Fallback normal
        // en dispositivos sin el modulo Magisk (o antes de activarlo),
        // con la mezcla Haas existente sin cambios.
        CAPTURE_ONLY,

        // Ambas rutas detectadas activas a la vez: Ruta B aparecio
        // mientras Ruta A seguia corriendo. Estado transitorio -- el
        // caller (PlaybackCaptureService) debe detener Ruta A al verlo.
        LOOPBACK_PROCESSING,

        // Ninguna de las dos rutas activa.
        DISABLED
    }

    private const val TAG = "RouteArbiter"

    // Mismo UUID exacto que omega_effect.cpp (OMEGA_EFFECT_UUID) y que
    // IvannaGlobalEffectManager.kt / el guard original de
    // CaptureEngine.start() ya usaban -- no se define uno nuevo, es la
    // unica fuente de verdad para identificar el efecto de sistema.
    private val OMEGA_SYSTEM_EFFECT_UUID: UUID =
        UUID.fromString("4956414e-4e41-4f4d-4547-415355505245")

    private val currentState = AtomicReference(RouteState.DISABLED)

    /** Ultimo estado calculado por evaluate(). No dispara una consulta nueva. */
    fun state(): RouteState = currentState.get()

    /**
     * Consulta si omega_effect (Ruta B) esta registrado como efecto de
     * sistema en este dispositivo. API publica, sin permisos nuevos --
     * identica a la que ya usaba el guard original en
     * CaptureEngine.start(). Segura de llamar desde un hilo de control
     * (hace binder call, no debe llamarse desde el hilo de audio).
     *
     * OJO: esto solo prueba REGISTRO, no procesamiento. Un efecto registrado
     * pero mudo (nunca inicializó su beacon) NO debe abortar Ruta A: para
     * decidir la propiedad del audio usar systemWideEffectProcessing().
     */
    /**
     * null means AudioEffect enumeration was unavailable. Callers controlling output
     * ownership must treat UNKNOWN as occupied by Route B (fail closed).
     */
    fun systemWideEffectPresence(): Boolean? = runCatching {
        AudioEffect.queryEffects()?.any { it.uuid == OMEGA_SYSTEM_EFFECT_UUID }
    }.getOrNull()

    /**
     * Evidencia REAL de que Ruta B está procesando, leída del beacon que el
     * propio efecto mantiene (DSPBridge.effectState() -> readEffectBeacon()).
     * A diferencia de systemWideEffectPresence() (solo registro en
     * AudioEffect.queryEffects()), esto prueba que el efecto está insertado y
     * habilitado en audioserver.
     *
     * FIX (Ruta B, Moto G85 2026-11-09): el efecto puede estar REGISTRADO pero
     * mudo (nunca inicializó su beacon porque no pudo crear
     * /data/local/tmp/omega_effect_beacon). Abortar Ruta A solo por registro
     * dejaba a IVANNA sin procesar en ninguna ruta. Misma política que
     * AudioBackendSelector.isRouteBConfirmed(): ENABLED_IDLE o PROCESSING
     * confirman Ruta B; NO_EFFECT o UNAVAILABLE (beacon ilegible/inexistente)
     * = sin evidencia => Ruta A NO aborta y retoma automáticamente si Ruta B
     * vuelve a quedar muda. Si la librería nativa no está cargada, el beacon
     * es ilegible (UNAVAILABLE) y se devuelve false: la decisión la reevalúa
     * el tick periódico cuando el nativo esté disponible.
     */
    fun systemWideEffectProcessing(): Boolean {
        return when (runCatching { DSPBridge.effectState() }.getOrNull()) {
            DSPBridge.EffectState.ENABLED_IDLE,
            DSPBridge.EffectState.PROCESSING -> true
            else -> false
        }
    }

    /** Legacy convenience only; safety-critical route owners must use the tri-state API. */
    fun isSystemWideEffectActive(): Boolean = systemWideEffectPresence() ?: false

    /**
     * Reevalua el estado combinando la evidencia de procesamiento de Ruta B
     * con si Ruta A esta corriendo actualmente. Pensado para llamarse
     * periodicamente desde un hilo de control (ver
     * PlaybackCaptureService.routeArbiterTick).
     *
     * FIX (Ruta B, Moto G85 2026-11-09): el estado se calcula con evidencia
     * REAL de procesamiento (systemWideEffectProcessing(), beacon), no con el
     * mero registro en queryEffects(). Un efecto registrado pero mudo ya no
     * clasifica como SYSTEM_WIDE_ONLY/LOOPBACK_PROCESSING: Ruta A sigue
     * (CAPTURE_ONLY) hasta que Ruta B demuestre que procesa de verdad.
     *
     * @param routeARunning si CaptureEngine esta activo en este momento.
     * @return el RouteState resultante.
     */
    fun evaluate(routeARunning: Boolean): RouteState {
        val processing = systemWideEffectProcessing()
        val presence = systemWideEffectPresence()
        val newState = when {
            processing -> if (routeARunning) RouteState.LOOPBACK_PROCESSING else RouteState.SYSTEM_WIDE_ONLY
            else -> if (routeARunning) RouteState.CAPTURE_ONLY else RouteState.DISABLED
        }
        val prev = currentState.getAndSet(newState)
        if (prev != newState) {
            Log.i(TAG, "[ROUTE_ARBITER] state=$prev -> $newState " +
                "system_wide_processing=$processing system_wide_registered=${presence ?: "UNKNOWN"} route_a_running=$routeARunning")
        }
        return newState
    }

    /** Reset explicito -- usado al detener el servicio por completo. */
    fun reset() {
        currentState.set(RouteState.DISABLED)
    }
}
