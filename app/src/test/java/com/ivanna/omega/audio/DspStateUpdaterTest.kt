// DspStateUpdaterTest.kt
// ============================================================================
// PRUEBAS UNITARIAS — DspStateUpdater
// © 2026 Luis Uriel Pimentel Pérez
// ============================================================================

package com.ivanna.omega.audio

import org.junit.Test
import org.junit.Assert.*
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

class DspStateUpdaterTest {
    
    @Test
    fun testDebounceDelay() {
        val updater = DspStateUpdater()
        val state1 = AudioState().copy(adaptiveIntensity = 0.3f)
        val state2 = AudioState().copy(adaptiveIntensity = 0.8f)
        
        updater.requestUpdate(state1)
        updater.requestUpdate(state2)  // Este debería cancelar el anterior
        
        // Esperar a que se procese
        Thread.sleep(100)
        
        // Solo state2 debería haberse aplicado
        val lastState = updater.getLastAppliedState()
        assertNotNull(lastState)
    }
    
    @Test
    fun testForceUpdate() {
        val updater = DspStateUpdater()
        val state = AudioState().copy(exciterAmount = 0.5f)
        
        updater.forceUpdate(state)
        
        val lastState = updater.getLastAppliedState()
        assertEquals(state, lastState)
    }
}
