package com.ivanna.omega.fotoholo

import org.junit.Assert.assertEquals
import org.junit.Test

class FotoHoloHrtfRuntimeStatusTest {
    private fun s(l: Boolean, on: Boolean?, d: Boolean, blank: Boolean = false) =
        FotoHoloTelemetryBridge.hrtfRuntimeStatus(l, on, d, blank)

    @Test fun `solo se declara activo si el motor lo lee activo y hay dataset`() {
        assertEquals("reported-active", s(true, true, true))
        assertEquals("engine-on-dataset-not-loaded", s(true, true, false))
    }

    @Test fun `con el motor apagado no se inventa actividad`() {
        assertEquals("dataset-loaded-hrtf-off", s(true, false, true))
        assertEquals("not-loaded", s(true, false, false))
    }

    @Test fun `sin lectura nativa se dice sin confirmar, no activo`() {
        assertEquals("dataset-loaded-active-path-unconfirmed", s(true, null, true))
        assertEquals("unavailable", s(true, null, false, blank = true))
        assertEquals("native-library-unavailable", s(false, true, true))
    }
}
