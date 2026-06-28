package com.ivanna.omega.dsp

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModel

class DSPViewModel : ViewModel() {
    var state by mutableStateOf(DSPState())
        private set

    fun updateState(block: (DSPState) -> DSPState) {
        state = block(state)
    }
}
