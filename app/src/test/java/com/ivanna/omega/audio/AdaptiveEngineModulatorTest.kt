// AdaptiveEngineModulatorTest.kt
// ============================================================================
// PRUEBAS UNITARIAS — AdaptiveEngineModulator
// © 2026 Luis Uriel Pimentel Pérez
// ============================================================================

package com.ivanna.omega.audio

import org.junit.Test
import org.junit.Assert.*

class AdaptiveEngineModulatorTest {
    
    @Test
    fun testNaturalModeCurve() {
        val modulator = AdaptiveEngineModulator()
        val baseState = AudioState().copy(exciterAmount = 0.5f)
        
        val result = modulator.modulateAdaptiveOutput(
            baseState,
            AdaptiveMode.NATURAL,
            0.5f
        )
        
        // NATURAL debe aplicar curva suave (exponente 0.8)
        assertTrue(result.exciterAmount >= 0f)
        assertTrue(result.exciterAmount <= 1f)
    }
    
    @Test
    fun testExtremeModeCurve() {
        val modulator = AdaptiveEngineModulator()
        val baseState = AudioState().copy(exciterAmount = 0.5f)
        
        val naturalResult = modulator.modulateAdaptiveOutput(
            baseState, AdaptiveMode.NATURAL, 0.5f
        )
        
        modulator.reset()
        
        val extremeResult = modulator.modulateAdaptiveOutput(
            baseState, AdaptiveMode.EXTREME, 0.5f
        )
        
        // EXTREME debe ser más agresivo que NATURAL
        assertTrue(extremeResult.exciterAmount >= naturalResult.exciterAmount)
    }
    
    @Test
    fun testSoftClipping() {
        val modulator = AdaptiveEngineModulator()
        val extremeState = AudioState().copy(
            masterGain = 10f,  // Valor extremo
            spatialWidth = 100f
        )
        
        val result = modulator.modulateAdaptiveOutput(
            extremeState, AdaptiveMode.EXTREME, 1.0f
        )
        
        // Soft clipping debe limitar los valores
        assertTrue(result.masterGain <= 2f)
        assertTrue(result.spatialWidth <= 2f)
    }
    
    @Test
    fun testOffModeProducesZero() {
        val modulator = AdaptiveEngineModulator()
        val baseState = AudioState().copy(exciterAmount = 1.0f)
        
        val result = modulator.modulateAdaptiveOutput(
            baseState, AdaptiveMode.OFF, 1.0f
        )
        
        // OFF mode debe producir factor 0
        assertEquals(0f, result.exciterAmount, 0.01f)
    }
}
