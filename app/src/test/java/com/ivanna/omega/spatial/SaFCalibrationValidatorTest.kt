package com.ivanna.omega.spatial

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNotSame
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Contrato de entrada de la calibración SAF (cinco errores finitos en [0,1]).
 * Antes, SaFOptimizer.runCalibrationStep() promediaba el array sin validar: vacío -> NaN,
 * NaN/Inf o fuera de rango -> errorEnergy contaminado, persistido y enviado al nativo.
 */
class SaFCalibrationValidatorTest {

    @Test fun acceptsExactlyFiveValuesInRangeAndReturnsDefensiveCopy() {
        val input = floatArrayOf(0.1f, 0.2f, 0.3f, 0.4f, 0.5f)
        val out = SaFCalibrationValidator.sanitize(input)
        assertNotNull(out)
        assertNotSame(input, out)
        input[0] = 0.9f                      // mutar el original no debe afectar a la copia
        assertEquals(0.1f, out!![0], 0f)
        assertEquals(0.3f, SaFCalibrationValidator.meanError(out), 1e-6f)
    }

    @Test fun boundariesZeroAndOneAreAccepted() {
        assertNotNull(SaFCalibrationValidator.sanitize(FloatArray(5) { 0f }))
        assertNotNull(SaFCalibrationValidator.sanitize(FloatArray(5) { 1f }))
    }

    @Test fun rejectsWrongLength() {
        assertNull(SaFCalibrationValidator.sanitize(floatArrayOf()))
        assertNull(SaFCalibrationValidator.sanitize(FloatArray(4) { 0.1f }))
        assertNull(SaFCalibrationValidator.sanitize(FloatArray(6) { 0.1f }))
    }

    @Test fun rejectsNonFinite() {
        for (bad in listOf(Float.NaN, Float.POSITIVE_INFINITY, Float.NEGATIVE_INFINITY)) {
            val a = FloatArray(5) { 0.2f }
            a[2] = bad
            assertNull("debe rechazar $bad", SaFCalibrationValidator.sanitize(a))
        }
    }

    @Test fun rejectsOutOfRange() {
        val high = FloatArray(5) { 0.2f }; high[4] = 1.0001f
        val low = FloatArray(5) { 0.2f }; low[0] = -0.0001f
        assertNull(SaFCalibrationValidator.sanitize(high))
        assertNull(SaFCalibrationValidator.sanitize(low))
    }

    @Test fun theArrayTheAppActuallySendsIsAccepted() {
        // SaFEngine.feedFeedback envía 0.2 (acierto) o 1.0 (fallo) en la dirección evaluada y 0 en el resto.
        for (feedbackError in listOf(0.2f, 1.0f)) {
            val a = FloatArray(5) { i -> if (i == 3) feedbackError else 0f }
            val out = SaFCalibrationValidator.sanitize(a)
            assertNotNull(out)
            assertTrue(SaFCalibrationValidator.meanError(out!!).isFinite())
        }
    }
}
