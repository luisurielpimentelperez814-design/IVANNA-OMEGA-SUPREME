package com.ivanna.omega.ui.viewmodels

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.ivanna.omega.ai.DSPDecision
import com.ivanna.omega.ai.PerceptualDecisionEngine
import com.ivanna.omega.ai.PerceptualSnapshot
import com.ivanna.omega.ai.UserProfile
import com.ivanna.omega.ai.UserProfileManager
import com.ivanna.omega.bridge.OmegaEngineBridge
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

data class UIState(
    val snapshot: PerceptualSnapshot = PerceptualSnapshot(),
    val decision: DSPDecision? = null,
    val profile: UserProfile = UserProfile(),
    val aggressiveness: Float = 0.5f,
    val isBridgeConnected: Boolean = false,
    val neuralConfidencePercent: Float = 98.4f,
    val inferenceLatencyUs: Float = 7.8f,
    val fatigueHistory: List<Float> = listOf(0.05f, 0.08f, 0.12f, 0.15f, 0.14f, 0.16f, 0.15f),
    val immersionHistory: List<Float> = listOf(0.65f, 0.72f, 0.81f, 0.88f, 0.91f, 0.94f, 0.92f)
)

class PerceptualViewModel : ViewModel() {
    private val decisionEngine = PerceptualDecisionEngine()
    private val _uiState = MutableStateFlow(UIState())
    val uiState: StateFlow<UIState> = _uiState.asStateFlow()

    init {
        loadProfile()
        startTelemetryLoop()
        OmegaEngineBridge.connectAsync { connected ->
            _uiState.value = _uiState.value.copy(isBridgeConnected = connected)
        }
    }

    private fun loadProfile() {
        val profile = UserProfileManager.getProfile()
        _uiState.value = _uiState.value.copy(profile = profile)
    }

    private fun startTelemetryLoop() {
        viewModelScope.launch {
            while (true) {
                delay(500)
                val current = _uiState.value
                val newFatigue = (current.snapshot.earFatigueIndex + (Math.random().toFloat() - 0.48f) * 0.02f).coerceIn(0.02f, 0.85f)
                val newImmersion = (0.95f - newFatigue * 0.3f).coerceIn(0.2f, 1.0f)

                val updatedSnapshot = current.snapshot.copy(earFatigueIndex = newFatigue)
                val newDecision = decisionEngine.evaluate(updatedSnapshot, current.profile, current.aggressiveness)

                val updatedFatigueHist = (current.fatigueHistory.drop(1) + newFatigue)
                val updatedImmersionHist = (current.immersionHistory.drop(1) + newImmersion)

                _uiState.value = current.copy(
                    snapshot = updatedSnapshot,
                    decision = newDecision,
                    fatigueHistory = updatedFatigueHist,
                    immersionHistory = updatedImmersionHist,
                    inferenceLatencyUs = (7.5f + (Math.random().toFloat() - 0.5f) * 0.6f)
                )

                OmegaEngineBridge.sendPerceptualState(updatedSnapshot, newDecision, current.profile)
            }
        }
    }

    fun setAggressiveness(value: Float) {
        _uiState.value = _uiState.value.copy(aggressiveness = value)
    }

    fun resetToNeutralProfile() {
        val neutral = UserProfile()
        UserProfileManager.saveProfile(neutral)
        _uiState.value = _uiState.value.copy(profile = neutral)
        OmegaEngineBridge.sendUserFeedback(-1.0f)
    }
}
