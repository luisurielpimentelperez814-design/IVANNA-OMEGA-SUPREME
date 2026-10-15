package com.ivanna.omega.ui.viewmodels

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.ivanna.omega.ai.AudioFeaturesInput
import com.ivanna.omega.ai.DSPDecision
import com.ivanna.omega.ai.PerceptualBrainEngine
import com.ivanna.omega.ai.PerceptualDecisionEngine
import com.ivanna.omega.ai.UserProfile
import com.ivanna.omega.ai.UserProfileManager
import com.ivanna.omega.audio.OmegaMetrics
import com.ivanna.omega.core.IvannaNativeLib
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlin.math.log10
import kotlin.math.max
import kotlin.math.sqrt

class PerceptualViewModel(application: Application) : AndroidViewModel(application) {

    private val profileManager = UserProfileManager(application)
    val perceptualEngine = PerceptualBrainEngine()
    val decisionEngine   = PerceptualDecisionEngine()

    private val _userProfile = MutableStateFlow(profileManager.loadProfile())
    val userProfile: StateFlow<UserProfile> = _userProfile.asStateFlow()

    data class UiState(
        // Rolling history — máximo 60 muestras (@ 500 ms = último minuto).
        // ANTES: hardcodeado a listOf(0.2f) y listOf(0.5f) — nunca se actualizaba.
        // AHORA: alimentado por el snapshot real de PerceptualBrainEngine.
        val fatigueHistory   : List<Float> = emptyList(),
        val immersionHistory : List<Float> = emptyList(),
        // ANTES: siempre 0 — no conectado.
        // AHORA: snapshot.confidence * 100 (YAMNet + PerceptualBrain).
        val neuralConfidencePercent : Int = 0,
        // ANTES: siempre false.
        // AHORA: OmegaMetrics.shared.dspActive || IvannaNativeLib.isLoaded.
        val isBridgeConnected : Boolean = false,
        // IPC real con el daemon del módulo Magisk (socket), distinto de "motor nativo cargado".
        val isMagiskConnected : Boolean = false
    )

    private val _uiState    = MutableStateFlow(UiState())
    val uiState: StateFlow<UiState> = _uiState.asStateFlow()

    private val _dspDecision = MutableStateFlow(DSPDecision())
    val dspDecision: StateFlow<DSPDecision> = _dspDecision.asStateFlow()

    companion object {
        private const val HISTORY_MAX = 60          // 60 muestras × 500 ms = 1 min
        private const val POLL_MS     = 500L
    }

    init {
        decisionEngine.updateProfile(_userProfile.value)

        // 1. Colectar decisiones del PerceptualDecisionEngine
        viewModelScope.launch {
            perceptualEngine.snapshot.collect { snapshot ->
                // Latencia REAL de la decisión (antes: 0.8 + (1-confianza)*0.5, inventada).
                val t0 = System.nanoTime()
                val decision = decisionEngine.evaluate(snapshot)
                val measuredMs = (System.nanoTime() - t0) / 1_000_000f
                val measured = decision.copy(executionLatencyMs = measuredMs)
                _dspDecision.value = measured
                decisionEngine.dispatchDecision(measured)
            }
        }

        // 2. Arrancar el motor perceptual y alimentarlo con telemetría real
        perceptualEngine.start()
        viewModelScope.launch(Dispatchers.IO) {
            while (true) {
                tick()
                delay(POLL_MS)
            }
        }
    }

    private fun tick() {
        // Fuente primaria: nativeGetAdaptiveTelemetry()
        // layout: [0]=rms [1]=peak [2]=grDb [3]=targetGain [4]=compAmount
        //         [5]=exciterRed [6]=spatialWidth [7]=safetyMargin
        //         [8]=voiceProtect [9]=applied
        val t = if (IvannaNativeLib.isLoaded)
            runCatching { IvannaNativeLib.nativeGetAdaptiveTelemetry() }.getOrNull()
        else null

        // Fuente secundaria: nativeGetAudioCharacteristics()
        // layout: [0]=rms [1]=peak [2]=percussiveness [3]=tonality
        //         [4]=reverb [5]=dynRange [6]=centroid [7]=spread
        val ac = if (IvannaNativeLib.isLoaded)
            runCatching { IvannaNativeLib.nativeGetAudioCharacteristics() }.getOrNull()
        else null

        // Construir AudioFeaturesInput desde datos reales
        val rmsLin    = t?.getOrElse(0) { 0f } ?: 0f
        val centroid  = ac?.getOrElse(6) { 2500f } ?: 2500f
        val spread    = ac?.getOrElse(7) { 800f }  ?: 800f
        val percuss   = ac?.getOrElse(2) { 0.3f }  ?: 0.3f
        val dynRange  = ac?.getOrElse(5) { 12f }   ?: 12f
        // crest factor estimado desde dynRange (lineal → dB proxy)
        val crestEst  = (dynRange * 1.5f).coerceIn(3f, 30f)

        if (rmsLin > 0f || ac != null) {
            perceptualEngine.processAudioFeatures(
                AudioFeaturesInput(
                    rms              = rmsLin,
                    lufs             = if (rmsLin > 1e-5f)
                        (20f * log10(max(rmsLin, 1e-5f)) - 3f).coerceIn(-60f, 0f) else -60f,
                    spectralCentroid = centroid.coerceIn(20f, 20000f),
                    spectralFlux     = spread / 10000f,
                    crestFactor      = crestEst,
                    transientDensity = percuss.coerceIn(0f, 1f)
                )
            )
        }

        // Actualizar UiState con snapshot real
        val snap = perceptualEngine.snapshot.value
        val confidencePct = (snap.confidence * 100f).toInt().coerceIn(0, 100)

        val metrics = OmegaMetrics.shared.value
        val bridgeOk = metrics.dspActive || IvannaNativeLib.isLoaded

        _uiState.value = _uiState.value.let { cur ->
            val newFat  = (cur.fatigueHistory   + snap.fatigue  ).takeLast(HISTORY_MAX)
            val newImm  = (cur.immersionHistory + snap.immersion).takeLast(HISTORY_MAX)
            cur.copy(
                fatigueHistory         = newFat,
                immersionHistory       = newImm,
                neuralConfidencePercent = confidencePct,
                isBridgeConnected      = bridgeOk,
                isMagiskConnected      = com.ivanna.omega.magisk.OmegaEngineBridge.isConnected
            )
        }
    }

    fun updateAggressiveness(aggressiveness: Float) {
        val updated = _userProfile.value.copy(aggressiveness = aggressiveness)
        _userProfile.value = updated
        profileManager.saveProfile(updated)
        decisionEngine.updateProfile(updated)
    }

    fun resetToNeutral() {
        val neutral = UserProfile()
        _userProfile.value = neutral
        profileManager.saveProfile(neutral)
        decisionEngine.updateProfile(neutral)
    }

    private var saveJob: kotlinx.coroutines.Job? = null

    /** Slider del Cortex: actualiza perfil + motor de decisión al instante y persiste con debounce. */
    fun setAggressiveness(value: Float) {
        val v = value.coerceIn(0f, 1f)
        val updated = _userProfile.value.copy(aggressiveness = v)
        _userProfile.value = updated
        decisionEngine.updateProfile(updated)
        saveJob?.cancel()
        saveJob = viewModelScope.launch(Dispatchers.IO) {
            delay(200L)
            profileManager.saveProfile(updated)
        }
        // Re-evalúa YA con el último snapshot para que el cambio se refleje sin esperar el próximo ciclo.
        val snap = perceptualEngine.snapshot.value
        val t0 = System.nanoTime()
        val d = decisionEngine.evaluate(snap)
        _dspDecision.value = d.copy(executionLatencyMs = (System.nanoTime() - t0) / 1_000_000f)
        decisionEngine.dispatchDecision(_dspDecision.value)
    }

    fun resetToNeutralProfile() {
        resetToNeutral()
        setAggressiveness(_userProfile.value.aggressiveness)
    }

    override fun onCleared() {
        perceptualEngine.release()
        super.onCleared()
    }
}
