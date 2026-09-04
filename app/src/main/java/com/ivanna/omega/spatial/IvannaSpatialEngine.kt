package com.ivanna.omega.spatial

import kotlin.math.*
import com.ivanna.omega.core.IvannaNativeLib

class IvannaSpatialEngine private constructor() {

    companion object {
        val shared: IvannaSpatialEngine = IvannaSpatialEngine()

        @Volatile var enabled: Boolean = true

        fun setAzimuth(rad: Float) { shared.azimuthRad = rad }
        fun setWidth(v: Float) { shared.widthFactor = v.coerceIn(0f, 1.5f) }
        fun setDistance(v: Float) { shared.distance = v.coerceIn(0.5f, 2.0f) }

        // FIX: elevación existía en UI (slider binauralElevation) y en prefs
        // pero no tenía ninguna función en el motor espacial ni llamada nativa.
        // Se modela como pitch del head-tracker: ±45° → nativeSetSpatialParams
        // con JSON {elevation_rad: v} que el motor HRTF ya acepta.
        fun setElevation(rad: Float) {
            shared.elevationRad = rad.coerceIn(-Math.PI.toFloat() / 4f, Math.PI.toFloat() / 4f)
            if (IvannaNativeLib.isLoaded) runCatching {
                IvannaNativeLib.nativeSetSpatialParams(
                    """{"azimuth_rad":${shared.azimuthRad},"elevation_rad":${shared.elevationRad}}"""
                )
            }
        }

        /**
         * Cablea el HeadTracker al motor nativo.
         * onOrientationChanged → nativeSetSpatialAngleRad (yaw) en el hilo
         * del sensor — zero-Compose, latencia mínima (~10 ms).
         */
        fun setHeadTracker(tracker: IvannaHeadTracker) =
            shared.attachHeadTracker(tracker)
    }

    private val sampleRate = 44100
    private val maxBlockSize = 4096
    private val maxItdSamples = (0.0015f * sampleRate).toInt()
    private val crossfeedDelaySamples = 44
    private val earlyReflectionTimes = intArrayOf(7, 13, 17, 23)
    private val earlyReflectionGains = floatArrayOf(0.3f, 0.2f, 0.15f, 0.1f)

    @Volatile var azimuthRad: Float = 0f
    @Volatile var elevationRad: Float = 0f
    @Volatile var widthFactor: Float = 1.0f
    @Volatile var distance: Float = 1.0f

    private var fadeGain = 1.0f
    private var targetEnabled = true
    private val fadeSpeed = 0.01f

    private var delayedL = FloatArray(maxItdSamples)
    private var delayedR = FloatArray(maxItdSamples)
    private var earlyReflectionBuffers = Array(4) { FloatArray(maxBlockSize) }
    private var crossfeedBufferL = FloatArray(crossfeedDelaySamples)
    private var crossfeedBufferR = FloatArray(crossfeedDelaySamples)

    private var itdWriteIndex = 0
    private var crossfeedWriteIndex = 0
    private var earlyWriteIndices = IntArray(4)
    private var modPhase = 0f
    private var erLowpassState = FloatArray(4)

    fun attachHeadTracker(tracker: IvannaHeadTracker) {
        tracker.onOrientationChanged = { pitch, yaw, roll ->
            if (IvannaNativeLib.isLoaded)
                IvannaNativeLib.nativeSetSpatialAngleRad(yaw)
            azimuthRad = yaw
        }
    }

    fun init(modelPath: String? = null) { reset() }

    fun reset() {
        delayedL.fill(0f); delayedR.fill(0f)
        crossfeedBufferL.fill(0f); crossfeedBufferR.fill(0f)
        earlyReflectionBuffers.forEach { it.fill(0f) }
        earlyWriteIndices.fill(0)
        erLowpassState.fill(0f)
        modPhase = 0f
        itdWriteIndex = 0; crossfeedWriteIndex = 0
        fadeGain = 1.0f; targetEnabled = true
        normGain = 1.0f
    }

    fun release() { reset() }

    /** Estado de la normalización constant-power (suavizado entre bloques). */
    private var normGain = 1.0f

    /**
     * FIX (ruta de volumen): la cadena espacial alteraba el nivel percibido.
     *   1. ILD usaba sqrt((1±cos az)/2) con clamp a 0.3 — en azimut 0 daba
     *      L=1.0 / R=0.3 (desbalance + caída de nivel) en vez de centro unitario.
     *   2. El ancho M/S (mid + side*w) sumaba energía al subir w.
     *   3. Las reflexiones tempranas se SUMABAN a la señal directa con ganancia
     *      ~0.5 → copia retardada audible = el eco al subir la intensidad.
     *   4. focusGain se calculaba y no se usaba.
     * Ahora: paneo constant-power unitario en el centro, ancho normalizado en
     * energía, reflexiones/crossfeed en mezcla wet/dry (no aditivas) y
     * normalización final constant-power contra el nivel de entrada. IVANNA
     * solo cambia la POSICIÓN espacial; el volumen es de Android/AudioManager.
     */
    fun processStereoInput(
        inL: FloatArray, inR: FloatArray,
        outL: FloatArray, outR: FloatArray,
        numFrames: Int
    ) {
        val desired = IvannaSpatialEngine.enabled
        if (desired != targetEnabled) targetEnabled = desired
        if (targetEnabled && fadeGain < 1f) fadeGain = (fadeGain + fadeSpeed).coerceAtMost(1f)
        else if (!targetEnabled && fadeGain > 0f) fadeGain = (fadeGain - fadeSpeed).coerceAtLeast(0f)

        if (fadeGain <= 0f) {
            System.arraycopy(inL, 0, outL, 0, numFrames)
            System.arraycopy(inR, 0, outR, 0, numFrames)
            return
        }

        val n = minOf(numFrames, maxBlockSize)
        // Cola sin procesar si el bloque excede el buffer máximo (antes la
        // condición `numFrames < n` era inalcanzable y dejaba basura en la cola).
        if (n < numFrames) {
            System.arraycopy(inL, n, outL, n, numFrames - n)
            System.arraycopy(inR, n, outR, n, numFrames - n)
        }

        val azimuth = azimuthRad
        val width = widthFactor.coerceIn(0f, 1.5f)
        val dist = distance
        val itdMax = maxItdSamples
        val sinAzimuth = sin(azimuth)

        // Paneo constant-power: pan = sin(az) ∈ [-1,1]; en el centro ambos
        // canales quedan exactamente a 1.0 (0 dB) — sin caída de nivel.
        val pan = sinAzimuth.coerceIn(-1f, 1f)
        val theta = (PI.toFloat() / 4f) * (1f + pan)
        val ildL = (sqrt(2f) * cos(theta)).coerceIn(0.25f, 1.42f)
        val ildR = (sqrt(2f) * sin(theta)).coerceIn(0.25f, 1.42f)

        // Ancho normalizado en energía: w = 1 → 1.0 (neutro).
        val widthNorm = sqrt(2f / (1f + width * width))
        // Reflexiones tempranas + crossfeed como mezcla wet/dry acotada.
        val erWet = 0.12f
        val distGain = 1f / sqrt(dist)

        var dryPow = 0.0
        var wetPow = 0.0
        val modAmount = if (abs(azimuth) > 0.1f) sin(modPhase) * 0.5f else 0f
        modPhase += 0.0003f

        for (i in 0 until n) {
            val itdSamples = (sinAzimuth * itdMax).toInt()
            val modOffset = (modAmount * 1.5f).toInt()
            val totalDelayL = maxOf(0, -itdSamples + modOffset)
            val totalDelayR = maxOf(0, itdSamples + modOffset)

            val readIdxL = (itdWriteIndex - totalDelayL + itdMax) % itdMax
            val readIdxR = (itdWriteIndex - totalDelayR + itdMax) % itdMax
            val delayedSampleL = delayedL[readIdxL]
            val delayedSampleR = delayedR[readIdxR]

            dryPow += (inL[i] * inL[i] + inR[i] * inR[i]).toDouble()

            delayedL[itdWriteIndex] = inL[i] * ildL
            delayedR[itdWriteIndex] = inR[i] * ildR
            itdWriteIndex = (itdWriteIndex + 1) % itdMax

            var sampleL = delayedSampleL
            var sampleR = delayedSampleR

            val mid = (sampleL + sampleR) * 0.5f
            val side = (sampleL - sampleR) * 0.5f
            sampleL = (mid + side * width) * widthNorm
            sampleR = (mid - side * width) * widthNorm

            var er = 0f
            for (j in 0 until 4) {
                val delaySamples = (earlyReflectionTimes[j] * sampleRate / 1000f).toInt() % maxBlockSize
                val readIdx = (earlyWriteIndices[j] - delaySamples + maxBlockSize) % maxBlockSize
                var reflected = earlyReflectionBuffers[j][readIdx] * earlyReflectionGains[j]
                erLowpassState[j] += 0.3f * (reflected - erLowpassState[j])
                reflected = erLowpassState[j]
                er += reflected
            }
            er *= 0.25f  // normaliza la suma de los 4 taps

            val xfeedFactor = 0.15f / dist
            val xfeedReadIdx = crossfeedWriteIndex % crossfeedDelaySamples
            val crossL = crossfeedBufferR[xfeedReadIdx] * xfeedFactor
            val crossR = crossfeedBufferL[xfeedReadIdx] * xfeedFactor

            // Mezcla wet/dry acotada: reflexiones y crossfeed NO añaden energía
            // sobre la señal directa (fuente real del eco reportado).
            var outSampleL = (sampleL * (1f - erWet) + (er + crossL) * erWet) * distGain
            var outSampleR = (sampleR * (1f - erWet) + (er + crossR) * erWet) * distGain

            outSampleL = outSampleL * fadeGain + inL[i] * (1f - fadeGain)
            outSampleR = outSampleR * fadeGain + inR[i] * (1f - fadeGain)

            wetPow += (outSampleL * outSampleL + outSampleR * outSampleR).toDouble()

            outL[i] = outSampleL
            outR[i] = outSampleR

            val erFeed = (sampleL + sampleR) * 0.5f
            for (j in 0 until 4) {
                earlyReflectionBuffers[j][earlyWriteIndices[j]] = erFeed
                earlyWriteIndices[j] = (earlyWriteIndices[j] + 1) % maxBlockSize
            }
            crossfeedBufferL[crossfeedWriteIndex] = sampleL
            crossfeedBufferR[crossfeedWriteIndex] = sampleR
            crossfeedWriteIndex = (crossfeedWriteIndex + 1) % crossfeedDelaySamples
        }

        // Normalización constant-power: iguala la energía de salida a la de
        // entrada, suavizada entre bloques. Ninguna posición espacial puede
        // subir ni bajar el volumen percibido.
        if (dryPow > 1e-9 && wetPow > 1e-9) {
            val target = sqrt(dryPow / wetPow).toFloat().coerceIn(0.5f, 2f)
            normGain += 0.25f * (target - normGain)
        } else {
            normGain += 0.25f * (1f - normGain)
        }
        for (i in 0 until n) {
            outL[i] = (outL[i] * normGain).coerceIn(-1f, 1f)
            outR[i] = (outR[i] * normGain).coerceIn(-1f, 1f)
        }
    }
}
