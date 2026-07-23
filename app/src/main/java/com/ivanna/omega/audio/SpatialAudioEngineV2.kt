package com.ivanna.omega.audio

import android.util.Log
import com.ivanna.omega.core.IvannaNativeLib

/**
 * SpatialAudioEngineV2 — motor binaural (32 objetos) del pipeline nativo.
 *
 * FIX (echo/choque de audio): la versión anterior capturaba el MICRÓFONO
 * FÍSICO (MediaRecorder.AudioSource.MIC), lo espacializaba y lo re-inyectaba
 * por AudioTrack en tiempo real. Esto producía un bucle de audio absurdo:
 * el micrófono del teléfono captaba la reproducción de las apps (o el ruido
 * ambiente) y la re-emitía espacializada por el altavoz, chocando con el
 * audio original → eco / duplicado / sonido horrible. El micrófono NUNCA
 * fue la fuente correcta: la señal que debe alimentar el motor espacial es
 * el audio de REPRODUCCIÓN real (MediaProjection), no el ambiente captado
 * por el mic.
 *
 * Ahora este motor es un PROCESADOR DE BLOQUES puro (sin AudioRecord/
 * AudioTrack propios). PlaybackCaptureService le entrega los bloques reales
 * capturados vía MediaProjection (audio de reproducción de las apps) y este
 * motor los pasa por nativeRenderSpatialBlock() para actualizar el estado
 * espacial nativo (posiciones/energía de los 32 objetos), consultable vía
 * IvannaNativeLib.nativeGetSpatialState(). No hay salida de audio propia:
 * el procesamiento audible real de otras apps ocurre vía
 * IvannaGlobalEffectManager (AudioEffect en la sesión de la app fuente),
 * exactamente igual que ya documenta PlaybackCaptureService. Este motor es
 * puramente de análisis/telemetría — consistente con ese mismo patrón,
 * evita cualquier eco y no duplica la reproducción.
 */
class SpatialAudioEngineV2 {
    var posX: Float = 0.0f
    var posY: Float = 0.0f
    var posZ: Float = 0.0f
    var mu: Float = 1.0f

    @Volatile private var isRunning = false
    private val bufferSize = 16
    private val sampleRate = 96000

    // Buffers de trabajo reutilizados entre bloques para no allocar por chunk.
    private val chunkIn = FloatArray(bufferSize)
    private val chunkOutL = FloatArray(bufferSize)
    private val chunkOutR = FloatArray(bufferSize)

    companion object {
        private const val TAG = "SpatialAudioEngineV2"

        // FIX: referencia compartida para que PlaybackCaptureService (otro
        // componente, mismo proceso) pueda alimentar la instancia activa sin
        // que MainActivity tenga que exponerla manualmente.
        @Volatile private var activeInstance: SpatialAudioEngineV2? = null

        fun feedCapturedBlock(interleavedStereo: FloatArray, numFrames: Int) {
            activeInstance?.processCapturedBlock(interleavedStereo, numFrames)
        }
    }

    val running: Boolean get() = isRunning

    fun start() {
        if (isRunning) return
        try {
            Log.i(TAG, "Iniciando motor binaural (modo análisis/telemetría)...")
            val initialized = IvannaNativeLib.nativeInitSpatialEngine(sampleRate, bufferSize)
            if (!initialized) {
                Log.e(TAG, "nativeInitSpatialEngine retornó false")
                return
            }
            isRunning = true
            activeInstance = this
            Log.i(TAG, "Motor nativo inicializado: sr=$sampleRate, bufSize=$bufferSize")
        } catch (e: Exception) {
            Log.e(TAG, "Error al iniciar motor binaural", e)
            isRunning = false
        }
    }

    fun stop() {
        try {
            Log.i(TAG, "Deteniendo motor binaural...")
            isRunning = false
            if (activeInstance === this) activeInstance = null
            try {
                val released = IvannaNativeLib.nativeReleaseSpatialEngine()
                if (released) {
                    Log.i(TAG, "Motor nativo liberado correctamente")
                } else {
                    Log.w(TAG, "nativeReleaseSpatialEngine retornó false")
                }
            } catch (e: Exception) {
                Log.e(TAG, "Error liberando motor nativo", e)
            }
            Log.i(TAG, "Motor binaural detenido")
        } catch (e: Exception) {
            Log.e(TAG, "Error crítico en stop()", e)
        }
    }

    /**
     * Procesa un bloque de audio REAL capturado (interleaved L,R desde
     * MediaProjection/PlaybackCaptureService), en chunks de [bufferSize]
     * muestras mono, actualizando el estado espacial nativo. No produce
     * audio de salida — es análisis puro, llamado desde el hilo de captura
     * de PlaybackCaptureService (Dispatchers.Default), nunca desde el hilo
     * principal.
     */
    @Synchronized
    fun processCapturedBlock(interleavedStereo: FloatArray, numFrames: Int) {
        if (!isRunning) return
        try {
            var offset = 0
            while (offset < numFrames) {
                val count = minOf(bufferSize, numFrames - offset)
                for (i in 0 until count) {
                    val idx = (offset + i) * 2
                    // downmix L/R capturado → mono para el motor espacial
                    chunkIn[i] = 0.5f * (interleavedStereo[idx] + interleavedStereo[idx + 1])
                }
                for (i in count until bufferSize) chunkIn[i] = 0f
                IvannaNativeLib.nativeRenderSpatialBlock(
                    chunkIn, chunkOutL, chunkOutR,
                    posX.toInt(), posY.toInt(), posZ.toInt(), mu.toInt()
                )
                offset += count
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error procesando bloque capturado", e)
        }
    }
}
