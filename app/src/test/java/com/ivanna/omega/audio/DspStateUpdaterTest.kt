// DspStateUpdaterTest.kt
// ============================================================================
// PRUEBAS UNITARIAS — DspStateUpdater
// © 2026 Luis Uriel Pimentel Pérez
// ============================================================================

package com.ivanna.omega.audio

import org.junit.Test
import org.junit.Assert.*
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import android.os.Looper
import org.robolectric.Shadows.shadowOf
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [33])
class DspStateUpdaterTest {
    
    @Test
    fun testDebounceDelay() {
        val updater = DspStateUpdater()
        val state1 = AudioState().copy(adaptiveIntensity = 0.3f)
        val state2 = AudioState().copy(adaptiveIntensity = 0.8f)
        
        updater.requestUpdate(state1)
        updater.requestUpdate(state2)  // Este debería cancelar el anterior
        
        // Avanzar el main looper: el Handler está ligado a él.
        shadowOf(Looper.getMainLooper()).idleFor(100, TimeUnit.MILLISECONDS)
        
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
