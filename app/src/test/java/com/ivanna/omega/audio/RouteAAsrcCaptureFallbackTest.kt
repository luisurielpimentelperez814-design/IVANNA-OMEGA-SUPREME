package com.ivanna.omega.audio

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Fallback de captura (v2.5.412): sin libivanna_omega.so cargada (en los
 * tests JVM la .so no existe), RouteAAsrc debe degradar a passthrough
 * exacto sin lanzar nada. Es el contrato en el que se apoya el port del
 * ASRC a la captura: si el nativo no carga, PlaybackCaptureService sigue
 * con micro-slips y el audio no se rompe (fail-closed).
 *
 * En el teléfono la .so sí carga y estos tests no aplican: ahí manda el
 * test host C++ (ASRC+Psi) y la telemetría en vivo (asrc=1, psi_active=1).
 */
class RouteAAsrcCaptureFallbackTest {

    @Test fun unavailableWithoutNativeLib() {
        val asrc = RouteAAsrc()
        assertFalse("sin .so el ASRC no está disponible", asrc.isAvailable)
        asrc.close()
    }

    @Test fun processIntoPassthrough320Frames() {
        val asrc = RouteAAsrc()
        val inBuf = FloatArray(320 * 2) { i -> (i % 7) * 0.01f }
        val outBuf = FloatArray(320 * 2)
        val produced = asrc.processInto(inBuf, 320, outBuf)
        assertEquals("el fallback devuelve los mismos frames que entraron", 320, produced)
        assertTrue("passthrough exacto (bit a bit)", inBuf.contentEquals(outBuf))
        asrc.close()
    }

    @Test fun processIntoPassthrough319FramesMicroSlip() {
        // Un micro-slip de MasterTiming deja 319 frames: el fallback debe
        // pasarlos tal cual, sin rellenar ni recortar.
        val asrc = RouteAAsrc()
        val inBuf = FloatArray(319 * 2) { i -> (i % 5) * 0.02f }
        val outBuf = FloatArray(319 * 2)
        val produced = asrc.processInto(inBuf, 319, outBuf)
        assertEquals(319, produced)
        assertTrue(inBuf.contentEquals(outBuf))
        asrc.close()
    }

    @Test fun settersAreNoOpsWithoutNative() {
        val asrc = RouteAAsrc()
        asrc.setSampleRate(48000f)
        asrc.setClockCorrectionPpm(-70.0)
        asrc.setPsiActive(true)
        asrc.setPsiAuto(true)
        asrc.setPsiDesignHold(true)
        asrc.setPsiGeometry(0.0875f, 0.30f, 0.5236f)
        asrc.setPsiFir(FloatArray(64), FloatArray(64))
        asrc.reset()
        asrc.close()
        asrc.close() // idempotente: segundo close no lanza
    }

    @Test fun gettersReturnDefaultsWithoutNative() {
        val asrc = RouteAAsrc()
        assertEquals(0.5, asrc.psiEta, 0.0)
        assertEquals(0.5, asrc.psiCoherence, 0.0)
        assertFalse(asrc.psiActive)
        assertEquals(0.0, asrc.clockCorrectionPpm, 0.0)
        asrc.close()
    }

    @Test fun processIntoZeroFramesReturnsZero() {
        val asrc = RouteAAsrc()
        assertEquals(0, asrc.processInto(FloatArray(64), 0, FloatArray(64)))
        asrc.close()
    }
}
