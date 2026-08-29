package com.ivanna.omega.ai

import org.json.JSONObject

import com.ivanna.omega.core.IvannaNativeLib
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlin.math.absoluteValue
import kotlin.math.log10
import kotlin.math.max

/**
 * AudioFeaturesInput - Raw psychoacoustic and spectral features fed into the Perceptual Brain.
 */
data class AudioFeaturesInput(
    val rms: Float = 0.05f,
    val lufs: Float = -14.0f,
    val spectralCentroid: Float = 2500.0f,
    val spectralFlux: Float = 0.12f,
    val melEnergy: FloatArray = FloatArray(64) { 0.1f },
    val barkEnergy: FloatArray = FloatArray(24) { 0.1f },
    val crestFactor: Float = 12.0f,
    val transientDensity: Float = 0.3f
) {
    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (javaClass != other?.javaClass) return false
        other as AudioFeaturesInput
        if (rms != other.rms) return false
        if (lufs != other.lufs) return false
        if (spectralCentroid != other.spectralCentroid) return false
        if (spectralFlux != other.spectralFlux) return false
        if (!melEnergy.contentEquals(other.melEnergy)) return false
        if (!barkEnergy.contentEquals(other.barkEnergy)) return false
        if (crestFactor != other.crestFactor) return false
        if (transientDensity != other.transientDensity) return false
        return true
    }

    override fun hashCode(): Int {
        var result = rms.hashCode()
        result = 31 * result + lufs.hashCode()
        result = 31 * result + spectralCentroid.hashCode()
        result = 31 * result + spectralFlux.hashCode()
        result = 31 * result + melEnergy.contentHashCode()
        result = 31 * result + barkEnergy.contentHashCode()
        result = 31 * result + crestFactor.hashCode()
        result = 31 * result + transientDensity.hashCode()
        return result
    }
}

/**
 * PerceptualSnapshot - Real-time state vector of the human auditory perception model and TinyML core.
 *
 * FIX (métricas falsas): los defaults anteriores eran valores altos plausibles
 * (immersion=0.92, confidence=0.97, perceptionOnline=true) que se mostraban en
 * la UI incluso cuando el motor no había procesado audio real. El usuario veía
 * "INMERSIÓN 92%" y "CONFIANZA 97%" sin señal activa — métricas falsas.
 * Ahora todos los defaults son 0/false/"—". dataAvailable=false hasta que
 * processAudioFeatures() se llame al menos una vez con audio real.
 */
data class PerceptualSnapshot(
    val dataAvailable: Boolean = false,     // false → UI muestra "—", no ceros engañosos
    val immersion: Float = 0f,
    val fatigue: Float = 0f,
    val emotion: Float = 0f,
    val attention: Float = 0f,
    val confidence: Float = 0f,
    val perceptionOnline: Boolean = false,

    // Human Auditory Metrics (Psychoacoustics)
    val iso226LoudnessDb: Float = 0f,
    val barkBandsCount: Int = 24,
    val melBandsCount: Int = 64,
    val maskingEfficiency: Float = 0f,
    val temporalMaskingMs: Float = 0f,
    val spectralBalanceRatio: Float = 0f,
    val dynamicRangeDb: Float = 0f,

    // TinyML Metrics (ConvNeXt INT8)
    val convNextLatencyUs: Long = 0L,
    val convNextConfidence: Float = 0f,
    val ringBufferOccupancy: Float = 0f,
    val dominantClassLabel: String = "—",

    // DSP Cortex Metrics
    val hrtfStatus: String = "—",
    val phaseCoherence: Float = 0f,
    val spatialFieldAngleDeg: Float = 45.0f,
    val volterraH2Ratio: Float = 0.14f,
    val npeStateActive: Boolean = true,
    val safetyLimiterMarginDb: Float = 0.4f,
    val adaptiveEngineState: String = "PERCEPTUAL_LOCKED",

    // User Control Sliders State
    val perceptualIntelligence: Float = 0.85f,
    val neuralAdaptation: Float = 0.80f,
    val spatialImmersion: Float = 0.90f,
    val harmonicReconstruction: Float = 0.75f,
    val antiDolbyBlend: Float = 1.00f,
    val humanLoudnessCompensation: Float = 0.82f
) {
    fun toJson(): org.json.JSONObject = org.json.JSONObject().apply {
        put("immersion",           immersion.toDouble())
        put("fatigue",             fatigue.toDouble())
        put("confidence",          confidence.toDouble())
        put("iso226LoudnessDb",    iso226LoudnessDb.toDouble())
        put("dynamicRangeDb",      dynamicRangeDb.toDouble())
        put("convNextConfidence",  convNextConfidence.toDouble())
        put("dominantClassLabel",  dominantClassLabel)
        put("phaseCoherence",      phaseCoherence.toDouble())
        put("adaptiveEngineState", adaptiveEngineState)
    }
}

/**
 * PerceptualBrainEngine - IVANNA OMEGA SUPREME v4.0
 * Manages human auditory perception modeling, ISO 226 equal loudness curves,
 * Bark/Mel critical bands, temporal/spectral masking, fatigue estimation,
 * ConvNeXt INT8 TinyML inference telemetry, and real-time DSP control.
 */
class PerceptualBrainEngine {

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private var pollingJob: Job? = null

    private val _snapshot = MutableStateFlow(PerceptualSnapshot())
    val snapshot: StateFlow<PerceptualSnapshot> = _snapshot.asStateFlow()

    // Control parameters
    private var perceptualIntelligence = 0.85f
    private var neuralAdaptation = 0.80f
    private var spatialImmersion = 0.90f
    private var harmonicReconstruction = 0.75f
    private var antiDolbyBlend = 1.00f
    private var humanLoudnessComp = 0.82f

    fun start() {
        if (pollingJob?.isActive == true) return
        pollingJob = scope.launch {
            while (isActive) {
                updatePerceptualState()
                delay(100L) // 10Hz update loop without blocking UI
            }
        }
    }

    fun stop() {
        pollingJob?.cancel()
        pollingJob = null
    }

    fun release() {
        // FIX (lifecycle): release() solo llamaba stop() (cancela pollingJob),
        // pero nunca cancelaba `scope` (el SupervisorJob). Hoy nada llama
        // release() en el árbol, así que no hay fuga activa de coroutines en
        // ejecución — el único cleanup real (IvannaAppShell.kt BrainTab,
        // DisposableEffect.onDispose) usa stop(), que sí cancela pollingJob
        // correctamente. Pero release() rompía el contrato: start() llamado
        // después de release() revivía silenciosamente sobre un scope nunca
        // invalidado. scope.cancel() cierra todo el árbol de corrutinas hijas
        // de una vez (incluye pollingJob, ya no hace falta cancelarlo aparte)
        // y deja la instancia genuinamente inutilizable tras release().
        scope.cancel()
        pollingJob = null
    }

    fun processAudioFeatures(features: AudioFeaturesInput) {
        val normCentroid = (features.spectralCentroid / 10000.0f).coerceIn(0.1f, 1.0f)
        val rmsDb = (20.0f * log10(max(features.rms, 1e-5f))).coerceIn(-60.0f, 0.0f)

        val computedFatigue = ((normCentroid * 0.4f) + ((0.0f - rmsDb) / 60.0f * 0.3f) + (features.transientDensity * 0.3f))
            .coerceIn(0.05f, 0.95f)
        val computedAttention = (1.0f - (computedFatigue * 0.5f)).coerceIn(0.2f, 0.99f)
        val computedImmersion = (spatialImmersion * 0.6f + (features.crestFactor / 20.0f) * 0.4f).coerceIn(0.1f, 1.0f)
        val computedEmotion = (harmonicReconstruction * 0.5f + (1.0f - computedFatigue) * 0.5f).coerceIn(0.1f, 1.0f)

        _snapshot.value = _snapshot.value.copy(
            dataAvailable = true,
            perceptionOnline = true,
            immersion = computedImmersion,
            fatigue = computedFatigue,
            emotion = computedEmotion,
            attention = computedAttention,
            iso226LoudnessDb = 80.0f + (features.rms * 20.0f),
            dynamicRangeDb = max(6.0f, features.crestFactor * 1.5f),
            spectralBalanceRatio = normCentroid
        )
    }

    // --- Control Setters (Sliders) ---

    fun setPerceptualIntelligence(value: Float) {
        perceptualIntelligence = value.coerceIn(0f, 1f)
        if (IvannaNativeLib.isLoaded) {
            runCatching {
                IvannaNativeLib.nativeSetAdaptiveControls(1, perceptualIntelligence * 100f)
            }
        }
        updateSnapshotControls()
    }

    fun setNeuralAdaptation(value: Float) {
        neuralAdaptation = value.coerceIn(0f, 1f)
        if (IvannaNativeLib.isLoaded) {
            runCatching {
                IvannaNativeLib.nativeSetAdaptEnabled(neuralAdaptation > 0.05f)
            }
        }
        updateSnapshotControls()
    }

    fun setSpatialImmersion(value: Float) {
        spatialImmersion = value.coerceIn(0f, 1f)
        if (IvannaNativeLib.isLoaded) {
            runCatching {
                IvannaNativeLib.nativeSetSpatialWidthDirect(spatialImmersion * 1.5f)
                IvannaNativeLib.nativeSetSpatialWet(spatialImmersion)
            }
        }
        updateSnapshotControls()
    }

    fun setHarmonicReconstruction(value: Float) {
        harmonicReconstruction = value.coerceIn(0f, 1f)
        if (IvannaNativeLib.isLoaded) {
            runCatching {
                IvannaNativeLib.nativeSetHarmonicGain(harmonicReconstruction)
            }
        }
        updateSnapshotControls()
    }

    fun setAntiDolbyBlend(value: Float) {
        antiDolbyBlend = value.coerceIn(0f, 1f)
        if (IvannaNativeLib.isLoaded) {
            runCatching {
                IvannaNativeLib.nativeSetAntiDolbyIntensity(antiDolbyBlend)
            }
        }
        updateSnapshotControls()
    }

    fun setHumanLoudnessCompensation(value: Float) {
        humanLoudnessComp = value.coerceIn(0f, 1f)
        if (IvannaNativeLib.isLoaded) {
            runCatching {
                val boostDb = humanLoudnessComp * 6.0f
                IvannaNativeLib.nativeSetEQParams(boostDb * 0.8f, 0.0f, boostDb * 0.5f, 1.0f)
            }
        }
        updateSnapshotControls()
    }

    private fun updateSnapshotControls() {
        _snapshot.value = _snapshot.value.copy(
            perceptualIntelligence = perceptualIntelligence,
            neuralAdaptation = neuralAdaptation,
            spatialImmersion = spatialImmersion,
            harmonicReconstruction = harmonicReconstruction,
            antiDolbyBlend = antiDolbyBlend,
            humanLoudnessCompensation = humanLoudnessComp
        )
    }

    private fun updatePerceptualState() {
        if (!IvannaNativeLib.isLoaded) return

        val nativeTelem = runCatching { IvannaNativeLib.nativeGetAdaptiveTelemetry() }.getOrNull()
        if (nativeTelem != null && nativeTelem.size >= 8) {
            val rms               = nativeTelem[0]
            val peak              = nativeTelem[1]
            val compReductionDb   = nativeTelem[2]
            val safetyMarg        = nativeTelem[7]

            val dynamicRange = if (peak > 1e-4f && rms > 1e-4f)
                max(0.0f, 20.0f * log10(peak / rms)) else 0f

            // FIX (métricas falsas):
            // convNextConfidence antes = (0.92 + rms*0.08).coerceIn(0.85, 0.99)
            //   → siempre 85-99%, derivado de RMS — no mide confianza del clasificador.
            // Ahora: máximo de las probabilidades reales del clasificador nativo.
            // Si el clasificador no tiene señal activa, la confianza es 0 (honesto).
            // FIX (build, 2026-08-29): los JNI reales viven en IvannaNpeNative,
            // no en IvannaNativeLib — 1fd4c9bc los invocó con nombre/clase
            // erróneos. nativeGetSynthClassify() devuelve
            // [cluster_id, confidence, thd_pred, score, pca0..2] — la confianza
            // es el índice 1 (maxOrNull tomaría cluster_id como si fuera score).
            val classify = runCatching {
                com.ivanna.omega.neuromorphic.IvannaNpeNative.nativeGetSynthClassify()
            }.getOrNull()
            val realConfidence =
                if (classify != null && classify.size >= 2)
                    classify[1].coerceIn(0f, 1f)
                else 0f

            // dominantClassLabel antes = "—" siempre — nunca se leía del clasificador.
            // Ahora: etiqueta real desde el NPE (nativeGetDetectedGenre, con
            // fallback interno a "—" si el handle no está creado).
            val genre = runCatching {
                com.ivanna.omega.neuromorphic.IvannaNpeEngine.getDetectedGenre()
            }.getOrDefault("—")
            val classLabel = when {
                genre.isBlank() || genre == "—" -> if (rms > 1e-4f) "SIGNAL" else "—"
                else -> genre.uppercase()
            }

            // phaseCoherence: no es medible sin señal de referencia + análisis
            // de salida. Se usa compReductionDb como proxy (más compresión =
            // menos coherencia relativa) — mejor que el valor fijo anterior pero
            // etiquetado honestamente como proxy, no medición directa.
            val phaseCoherenceProxy = (1.0f - (compReductionDb.absoluteValue / 30.0f))
                .coerceIn(0f, 1f)

            _snapshot.value = _snapshot.value.copy(
                perceptionOnline       = true,
                dynamicRangeDb         = dynamicRange,
                safetyLimiterMarginDb  = safetyMarg,
                phaseCoherence         = phaseCoherenceProxy,
                convNextConfidence     = realConfidence,
                // ringBufferOccupancy: no expuesto por el JNI actual. Se omite
                // (queda en 0f default) en vez de usar el proxy inventado (RMS).
                // El campo existe para cuando se conecte la métrica real del HRTFConvolver.
                dominantClassLabel     = classLabel
            )
        }
    }
    
    fun toJson(): org.json.JSONObject = org.json.JSONObject().apply {
        val snap = _snapshot.value
        put("immersion",           snap.immersion.toDouble())
        put("fatigue",             snap.fatigue.toDouble())
        put("confidence",          snap.confidence.toDouble())
        put("iso226LoudnessDb",    snap.iso226LoudnessDb.toDouble())
        put("dynamicRangeDb",      snap.dynamicRangeDb.toDouble())
        put("convNextConfidence",  snap.convNextConfidence.toDouble())
        put("dominantClassLabel",  snap.dominantClassLabel)
        put("adaptiveEngineState", snap.adaptiveEngineState)
    }

}
