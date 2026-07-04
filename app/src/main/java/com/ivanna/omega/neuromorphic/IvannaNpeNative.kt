/*
 * © 2026 Luis Uriel Pimentel Pérez — IVANNA N-P-E
 * All rights reserved. Proprietary and confidential.
 */
package com.ivanna.omega.neuromorphic

import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer

/**
 * Static JNI shim. Loads libomega_vibratory.so once and exposes the raw
 * native entry points. All higher-level wrappers (OmegaVibratoryProcessor,
 * IvannaNpeEngine) sit on top of this.
 */
object IvannaNpeNative {

    init { System.loadLibrary("ivanna_omega") }

    @JvmStatic external fun nativeCreate(sampleRate: Float, maxBlockFrames: Int): Long
    @JvmStatic external fun nativeDestroy(handle: Long)
    @JvmStatic external fun nativeReset(handle: Long)

    @JvmStatic external fun nativeProcess(
        handle: Long, inputBuffer: FloatBuffer, outputBuffer: FloatBuffer, numFrames: Int
    )

    @JvmStatic external fun nativeProcessStereo(
        handle: Long,
        inL: FloatBuffer, inR: FloatBuffer,
        outL: FloatBuffer, outR: FloatBuffer,
        numFrames: Int
    )

    @JvmStatic external fun nativeSetParameters(
        handle: Long,
        alpha: Float, beta: Float, gamma: Float, delta: Float,
        eta: Float, zeta: Float,
        srNoiseFloor: Float, syncThreshold: Float, noiseGainFar: Float,
        phi: Float, damping: Float, nonlinearity: Float, coupling: Float
    )

    @JvmStatic external fun nativeSetAGC(handle: Long, target: Float, rate: Float)
    @JvmStatic external fun nativeSetBypass(handle: Long, bypass: Boolean)
    @JvmStatic external fun nativeGetMetrics(handle: Long): FloatArray?

    @JvmStatic external fun nativeSnapshotScope(
        handle: Long, dst: FloatBuffer, maxFrames: Int
    ): Int

    /**
     * Activa/desactiva las tres etapas de procesamiento del motor en tiempo real.
     * Conecta los 6 toggles Neuro-Coclear de la UI (hrtf_enabled, cochlear_enabled,
     * adapt_enabled) que antes caían al else → Log.d().
     */
    @JvmStatic external fun nativeSetEngineFlags(
        handle: Long, hrtf: Boolean, cochlear: Boolean, adapt: Boolean
    )

    /**
     * Actualiza los cuatro parámetros del motor que no tenían ruta JNI:
     * harmonic_gain (slider "Ganancia Armónica" — clave corregida 'harm'),
     * lateral_inhib, ohc_compression y master_gain_db.
     */
    @JvmStatic external fun nativeSetNeuroParams(
        handle: Long,
        harmonicGain: Float,
        lateralInhib: Float,
        ohcCompression: Float,
        masterGainDb: Float
    )

    /**
     * Devuelve el género detectado por AutonomousBrain en la última ventana
     * analizada (~85 ms). Literal estático nativo: sin asignación de heap.
     * No requiere handle — consulta el estado global de g_brain.
     */
    @JvmStatic external fun nativeGetDetectedGenre(): String

    /**
     * Devuelve la firma de 5 bandas suavizada del Synthesizer (AutonomousBrain output).
     * float[5] = [sub_bass, mid_bass, mids, presence, brilliance] en dB.
     * Antes: computado en C++ y descartado. Ahora expuesto a UI.
     */
    @JvmStatic external fun nativeGetSynthSignature(): FloatArray

    /**
     * Clasificación del Synthesizer contra 3 centroides K-Means.
     * float[7] = [cluster_id, confidence, thd_pred, score, pca0, pca1, pca2]
     */
    @JvmStatic external fun nativeGetSynthClassify(): FloatArray

    @JvmStatic external fun nativeGetCopyright(): String
    @JvmStatic external fun nativeGetBuildTag(): String

    // ── Hexagon DSP Offloading API (v1.0.0) ──────────────────────────────────
    // Requiere libivanna_dsp_skel.so en /vendor/lib/rfsa/adsp/ y
    // libcdsprpc.so disponible en el dispositivo (SM6225 y afines).
    //
    // Flujo de uso:
    //   1. nativeDspOpen(sampleRate, nNeurons, blockSize) → true si disponible
    //   2. nativeDspSetActive(true)    → habilita offload en el callback de audio
    //   3. [procesar audio normalmente vía nativeProcessStereo]
    //   4. nativeDspGetMetrics()       → leer métricas del DSP
    //   5. nativeDspClose()            → cerrar al destruir el motor

    /**
     * Abre el handle FastRPC al cDSP y pre-asigna buffers ION para zero-copy.
     * @param sampleRate Frecuencia de muestreo en Hz (típicamente 48000)
     * @param nNeurons   Número de neuronas LIF en el DSP (≤ 256, pot. de 2)
     * @param blockSize  Frames por bloque (≤ 512, pot. de 2)
     * @return true si el DSP fue abierto exitosamente
     */
    @JvmStatic external fun nativeDspOpen(
        sampleRate: Int, nNeurons: Int, blockSize: Int
    ): Boolean

    /**
     * Cierra el handle FastRPC y libera todos los buffers ION.
     * El audio fallback a CPU automáticamente en el siguiente bloque.
     */
    @JvmStatic external fun nativeDspClose()

    /**
     * @return true si el cDSP está abierto y listo para procesar audio.
     */
    @JvmStatic external fun nativeDspIsAvailable(): Boolean

    /**
     * Habilita o deshabilita el offloading al DSP sin cerrar el handle.
     * Útil para comparar CPU vs DSP en caliente.
     */
    @JvmStatic external fun nativeDspSetActive(active: Boolean)

    /**
     * Actualiza los parámetros neuro en el DSP (lock-free, cualquier hilo).
     * Se aplican al inicio del siguiente bloque de audio en el DSP.
     */
    @JvmStatic external fun nativeDspSetNeuroParams(
        alpha: Float, beta: Float, gamma: Float, delta: Float, eta: Float,
        lateralInhib: Float, ohcCompression: Float, masterGainDb: Float
    )

    /**
     * Métricas del último bloque procesado por el cDSP.
     * @return FloatArray(8): [cpuLoad, rmsOut, agcGain, spectralEntropy,
     *                         lifFireRateHz, hvxCycles, vtcmBytesUsed, reserved]
     *         null si el DSP no está activo.
     */
    @JvmStatic external fun nativeDspGetMetrics(): FloatArray?

    fun allocFloatBuffer(capacity: Int): FloatBuffer =
        ByteBuffer.allocateDirect(capacity * Float.SIZE_BYTES)
            .order(ByteOrder.nativeOrder())
            .asFloatBuffer()
}
