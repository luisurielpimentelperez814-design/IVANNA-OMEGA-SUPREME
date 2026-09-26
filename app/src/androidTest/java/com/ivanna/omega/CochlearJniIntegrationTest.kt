package com.ivanna.omega

import androidx.test.ext.junit.runners.AndroidJUnit4
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.core.NativeBridge
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/**
 * CochlearJniIntegrationTest — Test de integración end-to-end (Kotlin -> JNI -> C++20).
 * Valida el ciclo completo de vida del Eje Supremo Coclear (CochlearActiveInverseEngine).
 */
@RunWith(AndroidJUnit4::class)
class CochlearJniIntegrationTest {

    @Before
    fun setUp() {
        if (IvannaNativeLib.isLoaded) {
            IvannaNativeLib.nativeInitDSP(48000)
        }
    }

    @Test
    fun cochlearEnableDisable_propagatesViaJni() {
        if (!NativeBridge.isLoaded) {
            // Guard si el entorno de test no tiene libivanna_omega.so cargado
            return
        }

        // 1. Activar Eje Supremo Coclear
        NativeBridge.setCochlearInverseEnabled(true)
        assertTrue(
            "Cochlear Active Inverse debe figurar activo tras setCochlearInverseEnabled(true)",
            NativeBridge.isCochlearActive()
        )

        // 2. Calibrar intensidad de reconstrucción
        NativeBridge.setCochlearIntensity(0.5f)
        assertEquals(
            "La intensidad debe reflejarse en 0.5f",
            0.5f,
            NativeBridge.getCochlearIntensity(),
            0.01f
        )

        // 3. Procesar un bloque de audio estéreo y verificar ausencia de crashes y NaNs
        val frames = 256
        val inL = FloatArray(frames) { i -> Math.sin(2.0 * Math.PI * 440.0 * i / 48000.0).toFloat() * 0.5f }
        val inR = FloatArray(frames) { i -> Math.cos(2.0 * Math.PI * 440.0 * i / 48000.0).toFloat() * 0.5f }
        val outL = FloatArray(frames)
        val outR = FloatArray(frames)

        IvannaNativeLib.nativeProcessBlock(inL, inR, outL, outR, frames)

        // Verificar estabilidad numérica (cero NaNs / Infs)
        for (i in 0 until frames) {
            assertTrue("outL no debe ser NaN ni Inf en muestra $i", outL[i].isFinite())
            assertTrue("outR no debe ser NaN ni Inf en muestra $i", outR[i].isFinite())
        }

        // 4. Desactivar y verificar propagación inmediata
        NativeBridge.setCochlearInverseEnabled(false)
        assertFalse(
            "Cochlear Active Inverse debe figurar inactivo tras setCochlearInverseEnabled(false)",
            NativeBridge.isCochlearActive()
        )
    }

    @Test
    fun cochlearClampingAndEdgeCases() {
        if (!NativeBridge.isLoaded) return

        // Test de clampeo de intensidad defensivo
        NativeBridge.setCochlearIntensity(-2.0f)
        assertEquals(0.0f, NativeBridge.getCochlearIntensity(), 0.001f)

        NativeBridge.setCochlearIntensity(5.0f)
        assertEquals(1.0f, NativeBridge.getCochlearIntensity(), 0.001f)
    }
}
