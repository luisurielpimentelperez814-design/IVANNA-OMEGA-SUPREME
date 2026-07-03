package com.ivanna.omega.neuromorphic

import android.util.Log
import java.nio.FloatBuffer

/**
 * IvannaNpeEngine — wrapper de IvannaNpeNative.
 * Único punto de entrada al motor NHO+LIF+BiquadEnvelopeBank+AutonomousBrain.
 * Se llama desde PlaybackCaptureService, en el mismo hilo de audio real que
 * ya ejecuta DSPBridge.process() — impacto audible directo.
 */
object IvannaNpeEngine {
    private const val TAG = "IvannaNpeEngine"

    private var handle: Long = 0L
    private var maxFrames: Int = 0

    private var bufInL: FloatBuffer? = null
    private var bufInR: FloatBuffer? = null
    private var bufOutL: FloatBuffer? = null
    private var bufOutR: FloatBuffer? = null

    val isReady: Boolean get() = handle != 0L

    fun init(sampleRate: Int, maxBlockFrames: Int) {
        if (handle != 0L) return
        handle = IvannaNpeNative.nativeCreate(sampleRate.toFloat(), maxBlockFrames)
        if (handle == 0L) {
            Log.e(TAG, "nativeCreate devolvió handle nulo")
            return
        }
        maxFrames = maxBlockFrames
        bufInL = IvannaNpeNative.allocFloatBuffer(maxBlockFrames)
        bufInR = IvannaNpeNative.allocFloatBuffer(maxBlockFrames)
        bufOutL = IvannaNpeNative.allocFloatBuffer(maxBlockFrames)
        bufOutR = IvannaNpeNative.allocFloatBuffer(maxBlockFrames)
        Log.i(TAG, "init sr=$sampleRate maxBlockFrames=$maxBlockFrames handle=$handle")
    }

    /**
     * Procesa in-place un buffer intercalado estéreo (L,R,L,R,...) de
     * PlaybackCaptureService. numFrames = muestras por canal.
     */
    fun processInterleavedStereo(buffer: FloatArray, numFrames: Int) {
        if (handle == 0L || numFrames <= 0 || numFrames > maxFrames) return
        val inL = bufInL ?: return
        val inR = bufInR ?: return
        val outL = bufOutL ?: return
        val outR = bufOutR ?: return

        inL.clear(); inR.clear(); outL.clear(); outR.clear()
        for (i in 0 until numFrames) {
            inL.put(buffer[i * 2])
            inR.put(buffer[i * 2 + 1])
        }
        inL.flip(); inR.flip()

        IvannaNpeNative.nativeProcessStereo(handle, inL, inR, outL, outR, numFrames)

        for (i in 0 until numFrames) {
            buffer[i * 2] = outL.get(i)
            buffer[i * 2 + 1] = outR.get(i)
        }
    }

    fun setBypass(bypass: Boolean) {
        if (handle != 0L) IvannaNpeNative.nativeSetBypass(handle, bypass)
    }

    fun setAGC(targetDb: Float, rate: Float) {
        if (handle != 0L) IvannaNpeNative.nativeSetAGC(handle, targetDb, rate)
    }

    fun setEngineFlags(hrtf: Boolean, cochlear: Boolean, adapt: Boolean) {
        if (handle != 0L) IvannaNpeNative.nativeSetEngineFlags(handle, hrtf, cochlear, adapt)
    }

    fun setNeuroParams(harmonicGain: Float, lateralInhib: Float, ohcCompression: Float, masterGainDb: Float) {
        if (handle != 0L) {
            IvannaNpeNative.nativeSetNeuroParams(handle, harmonicGain, lateralInhib, ohcCompression, masterGainDb)
        }
    }

    fun getDetectedGenre(): String =
        if (handle != 0L) IvannaNpeNative.nativeGetDetectedGenre() else "\u2014"

    /** [sub_bass, mid_bass, mids, presence, brilliance] en dB */
    fun getSynthSignature(): FloatArray =
        if (handle != 0L) IvannaNpeNative.nativeGetSynthSignature() else FloatArray(5)

    /** [cpuLoad, rmsOut, agcGain, spectralEntropy, lifFireRateHz, transientCue, spatialCue, residualCue] */
    fun getMetrics(): FloatArray =
        if (handle != 0L) (IvannaNpeNative.nativeGetMetrics(handle) ?: FloatArray(8)) else FloatArray(8)

    fun reset() {
        if (handle != 0L) IvannaNpeNative.nativeReset(handle)
    }

    fun release() {
        if (handle != 0L) {
            IvannaNpeNative.nativeDestroy(handle)
            handle = 0L
        }
        bufInL = null; bufInR = null; bufOutL = null; bufOutR = null
    }
}
