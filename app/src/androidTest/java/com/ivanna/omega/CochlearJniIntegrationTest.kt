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

    @Test
    fun supremeFiveAxes_jniControlsAndTelemetry() {
        if (!NativeBridge.isLoaded) return

        // Eje 1: Warped Lattice
        NativeBridge.safeSetWarpedLatticeLambda(0.756f)
        NativeBridge.safeSetWarpedLatticeBlDrive(0.50f)
        NativeBridge.safeSetWarpedLatticeMicroChirp(true)
        NativeBridge.safeSetWarpedLatticeEnabled(true)
        val tauG = NativeBridge.safeGetWarpedLatticeSubSampleDelay()
        assertTrue("Sub-sample delay debe estar en [0, 1)", tauG in 0.0f..1.0f)

        // Eje 2: Transharmonic CVNN + DDSP
        NativeBridge.safeSetTransharmonicHarmonicGain(0.30f)
        NativeBridge.safeSetTransharmonicImdCancel(0.85f)
        NativeBridge.safeSetTransharmonicCvnnEnabled(true)
        assertTrue(NativeBridge.safeGetTransharmonicPhaseStep().isFinite())

        // Eje 3: SNN INT8 + NMF -> HOA 4th Order
        NativeBridge.safeSetSnnHoaImmersivity(0.65f)
        NativeBridge.safeSetSnnSpikeThreshold(0.55f)
        NativeBridge.safeSetSnnHoaUpmixerEnabled(true)
        assertTrue(NativeBridge.safeGetSnnActiveSpikes() >= 0)

        // Eje 4: Pinna Manifold INR-SDF
        NativeBridge.safeCalibratePinnaManifold(0.15f, -0.10f, 0.08f)
        NativeBridge.safeSetPinnaManifoldWetMix(0.65f)
        NativeBridge.safeSetPinnaManifoldEnabled(true)
        assertTrue("Pinna notch en banda anatómica", NativeBridge.safeGetPinnaActiveNotchHz() in 6000f..11000f)
        assertTrue("ITD en rango fisiológico", NativeBridge.safeGetPinnaActiveItdUs() in 450f..800f)

        // Eje 5: SHM Lockless CAS + Farrow 5th-Order MSO
        NativeBridge.safeSetMsoItdNanoseconds(8500f)
        NativeBridge.safeSetEbpfBypassActive(true)
        NativeBridge.safeSetFarrowMsoEnabled(true)
        assertTrue(NativeBridge.safeAcquireShmArbitration(7777))
        assertEquals(7777, NativeBridge.safeGetShmOwnerPid())
        assertTrue(NativeBridge.safeReleaseShmArbitration(7777))

        // Calibración activa NLMS y fotogramétrica 8x8 + Telemetría extendida
        NativeBridge.safeRunWarpedLatticeLoopbackCalibration(92.0f, 0.008f)
        val kappas = NativeBridge.safeGetWarpedLatticeKappas()
        assertEquals("El filtro de celosía deformada tiene 8 etapas", 8, kappas.size)
        for (k in kappas) {
            assertTrue("Estabilidad de Schur |kappa_m| < 1", kotlin.math.abs(k) < 1.0f)
        }

        val masks = NativeBridge.safeGetSnnOrthogonalMasks()
        assertEquals("SNN+NMF descompone en 4 flujos ortogonales", 4, masks.size)

        val patch64 = FloatArray(64) { idx -> 0.5f + 0.25f * kotlin.math.sin(idx * 0.3f) }
        NativeBridge.safeCalibratePinnaFromImagePatch(patch64)
        val latents = NativeBridge.safeGetPinnaActiveLatents()
        assertEquals("Pinna INR proyecta 6 parámetros antropométricos latentes", 6, latents.size)

        // Restaurar bypass
        NativeBridge.safeSetWarpedLatticeEnabled(false)
        NativeBridge.safeSetTransharmonicCvnnEnabled(false)
        NativeBridge.safeSetSnnHoaUpmixerEnabled(false)
        NativeBridge.safeSetPinnaManifoldEnabled(false)
        NativeBridge.safeSetFarrowMsoEnabled(false)
    }
}
