/*
 * © 2026 Luis Uriel Pimentel Pérez — IVANNA N-P-E
 * All rights reserved. Proprietary and confidential.
 *
 * IvannaDspManager.kt — Gestor del offloading al cDSP Hexagon (v1.0.0)
 *
 * Responsabilidades:
 *   • Detectar disponibilidad del cDSP en el dispositivo
 *   • Gestionar el ciclo de vida: open → setActive → close
 *   • Sincronizar parámetros del motor CPU → DSP cuando el DSP está activo
 *   • Exponer métricas en tiempo real para la UI (StateFlow)
 *   • Auto-tuning: ajustar blockSize según la latencia AAudio medida
 */
package com.ivanna.omega.neuromorphic

import android.content.Context
import android.util.Log
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*

private const val TAG = "IvannaDspManager"

/** Métricas devueltas por el cDSP en cada polling tick. */
data class DspMetrics(
    val cpuLoadRatio   : Float = 0f,
    val rmsOut         : Float = 0f,
    val agcGain        : Float = 1f,
    val spectralEntropy: Float = 0f,
    val lifFireRateHz  : Float = 0f,
    val hvxCycles      : Float = 0f,
    val vtcmBytesUsed  : Float = 0f,
    val available      : Boolean = false
)

/** Estado del DSP expuesto a la UI. */
data class DspState(
    val opened  : Boolean    = false,
    val active  : Boolean    = false,
    val metrics : DspMetrics = DspMetrics()
)

object IvannaDspManager {

    // ── Configuración ────────────────────────────────────────────────────────

    /** Frecuencia de muestreo del motor principal (debe coincidir con AAudio). */
    var sampleRate : Int = 48_000

    /** Neuronas LIF en el DSP. Valor recomendado: 64 (balance carga/calidad). */
    var nNeurons   : Int = 64

    /**
     * Tamaño de bloque en el DSP.
     * Debe ser ≤ blockSize de AAudio para evitar latencia adicional.
     * Recomendado: 256 frames → ~5.3 ms @ 48 kHz.
     */
    var blockSize  : Int = 256

    // ── Estado observable ────────────────────────────────────────────────────

    private val _state = MutableStateFlow(DspState())
    val state: StateFlow<DspState> = _state.asStateFlow()

    private var pollingJob: Job? = null
    private val scope = CoroutineScope(Dispatchers.IO + SupervisorJob())

    // ── Lifecycle ────────────────────────────────────────────────────────────

    /**
     * Intenta abrir el cDSP Hexagon.
     * Llamar después de que el motor de audio esté inicializado.
     * @return true si el DSP quedó disponible.
     */
    fun open(): Boolean {
        Log.i(TAG, "Intentando abrir cDSP — sr=$sampleRate n=$nNeurons block=$blockSize")
        val ok = IvannaNpeNative.nativeDspOpen(sampleRate, nNeurons, blockSize)
        if (ok) {
            Log.i(TAG, "cDSP abierto exitosamente")
            _state.value = _state.value.copy(opened = true, active = false)
            startMetricsPolling()
        } else {
            Log.w(TAG, "cDSP no disponible en este dispositivo (se requiere SM6225/Hexagon 680)")
            _state.value = DspState(opened = false, active = false, metrics = DspMetrics(available = false))
        }
        return ok
    }

    /** Habilita el offloading al DSP en el siguiente bloque de audio. */
    fun enable() {
        if (!_state.value.opened) {
            Log.w(TAG, "enable() ignorado — DSP no abierto")
            return
        }
        IvannaNpeNative.nativeDspSetActive(true)
        _state.value = _state.value.copy(active = true)
        Log.i(TAG, "DSP offloading HABILITADO")
    }

    /** Deshabilita el offloading (audio vuelve a CPU en el siguiente bloque). */
    fun disable() {
        IvannaNpeNative.nativeDspSetActive(false)
        _state.value = _state.value.copy(active = false)
        Log.i(TAG, "DSP offloading deshabilitado — CPU fallback activo")
    }

    /** Cierra el handle FastRPC y libera todos los recursos. */
    fun close() {
        stopMetricsPolling()
        IvannaNpeNative.nativeDspClose()
        _state.value = DspState()
        Log.i(TAG, "cDSP cerrado")
    }

    // ── Sincronización de parámetros ─────────────────────────────────────────

    /**
     * Envía los parámetros del motor al DSP.
     * Llamar cada vez que el usuario mueve un slider, DESPUÉS de llamar
     * nativeSetNeuroParams/nativeSetParameters para el motor CPU.
     * Es lock-free: puede llamarse desde cualquier hilo.
     */
    fun syncNeuroParams(
        alpha          : Float = 0.5f,
        beta           : Float = 0.5f,
        gamma          : Float = 0.5f,
        delta          : Float = 0.5f,
        eta            : Float = 0.5f,
        lateralInhib   : Float = 0.3f,
        ohcCompression : Float = 0.5f,
        masterGainDb   : Float = 0.0f
    ) {
        if (!_state.value.opened) return
        IvannaNpeNative.nativeDspSetNeuroParams(
            alpha, beta, gamma, delta, eta,
            lateralInhib, ohcCompression, masterGainDb
        )
    }

    // ── Métricas polling ─────────────────────────────────────────────────────

    private fun startMetricsPolling() {
        pollingJob?.cancel()
        pollingJob = scope.launch {
            while (isActive) {
                delay(100L) // 10 Hz de actualización de métricas
                val raw = IvannaNpeNative.nativeDspGetMetrics()
                val m = if (raw != null && raw.size >= 8) {
                    DspMetrics(
                        cpuLoadRatio    = raw[0],
                        rmsOut          = raw[1],
                        agcGain         = raw[2],
                        spectralEntropy = raw[3],
                        lifFireRateHz   = raw[4],
                        hvxCycles       = raw[5],
                        vtcmBytesUsed   = raw[6],
                        available       = true
                    )
                } else {
                    DspMetrics(available = false)
                }
                _state.value = _state.value.copy(metrics = m)
            }
        }
    }

    private fun stopMetricsPolling() {
        pollingJob?.cancel()
        pollingJob = null
    }

    // ── Auto-tuning ──────────────────────────────────────────────────────────

    /**
     * Ajusta el blockSize del DSP para que coincida con el burst size
     * reportado por AAudio. Llamar después de que el stream AAudio
     * haya sido creado y se conozca el burst size real.
     *
     * No requiere cerrar y reabrir el DSP: actualiza internamente el
     * parámetro para la próxima apertura (o llama open() de nuevo).
     */
    fun autoTuneBlockSize(aAudioBurstSize: Int) {
        // El blockSize del DSP debe ser ≤ burstSize y pot. de 2
        var bs = 32
        while (bs * 2 <= aAudioBurstSize && bs * 2 <= 512) bs *= 2
        if (bs != blockSize) {
            Log.i(TAG, "Auto-tune blockSize: $blockSize → $bs (AAudio burst=$aAudioBurstSize)")
            blockSize = bs
        }
    }

    /**
     * Consulta si el DSP está activo y procesando audio.
     * Equivale a state.value.active && nativeDspIsAvailable().
     */
    val isProcessing: Boolean
        get() = _state.value.active && IvannaNpeNative.nativeDspIsAvailable()
}
