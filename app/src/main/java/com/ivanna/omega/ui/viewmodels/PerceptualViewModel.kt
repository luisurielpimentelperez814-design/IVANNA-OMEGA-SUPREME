package com.ivanna.omega.ui.viewmodels

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.ivanna.omega.ai.DSPDecision
import com.ivanna.omega.ai.PerceptualBrainEngine
import com.ivanna.omega.ai.PerceptualDecisionEngine
import com.ivanna.omega.ai.UserProfile
import com.ivanna.omega.ai.UserProfileManager
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

class PerceptualViewModel(application: Application) : AndroidViewModel(application) {
    private val profileManager = UserProfileManager(application)
    val perceptualEngine = PerceptualBrainEngine()
    val decisionEngine = PerceptualDecisionEngine()

    private val _userProfile = MutableStateFlow(profileManager.loadProfile())
    val userProfile: StateFlow<UserProfile> = _userProfile.asStateFlow()

    private val _dspDecision = MutableStateFlow(DSPDecision())
    val dspDecision: StateFlow<DSPDecision> = _dspDecision.asStateFlow()

    init {
        decisionEngine.updateProfile(_userProfile.value)
        viewModelScope.launch {
            perceptualEngine.snapshot.collect { snapshot ->
                val decision = decisionEngine.evaluate(snapshot)
                _dspDecision.value = decision
                decisionEngine.dispatchDecision(decision)
            }
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


    fun setAggressiveness(value: Float) {
        _dspDecision.value = _dspDecision.value.copy(
            confidence = value.coerceIn(0f, 1f)
        )
    }

    fun resetToNeutralProfile() {
        _dspDecision.value = DSPDecision()
    }
}


