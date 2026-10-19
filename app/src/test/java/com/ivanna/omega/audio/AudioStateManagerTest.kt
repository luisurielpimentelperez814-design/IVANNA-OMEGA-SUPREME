// AudioStateManagerTest.kt
// ============================================================================
// PRUEBAS UNITARIAS — AudioStateManager
// © 2026 Luis Uriel Pimentel Pérez
// ============================================================================

package com.ivanna.omega.audio

import org.junit.Test
import org.junit.Assert.*
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [33])
class AudioStateManagerTest {
    
    @Test
    fun testInitialState() {
        val state = AudioState()
        assertEquals(AdaptiveMode.STUDIO, state.adaptiveMode)
        assertEquals(0.82f, state.adaptiveIntensity, 0.01f)
        assertTrue(state.binaural)
        assertFalse(state.manifold)
    }
    
    @Test
    fun testParameterRanges() {
        AudioStateManager.updateState {
            it.copy(
                adaptiveIntensity = 1.5f,  // Fuera de rango
                spatialWidth = 3.0f,
                masterGain = 5.0f
            )
        }
        
        val state = AudioStateManager.audioState.value
        assertTrue(state.adaptiveIntensity in 0f..1f)
        assertTrue(state.spatialWidth in 0f..2f)
        assertTrue(state.masterGain in 0.1f..2f)
    }
}
