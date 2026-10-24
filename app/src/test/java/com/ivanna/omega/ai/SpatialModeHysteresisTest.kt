package com.ivanna.omega.ai

import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * Verifica el fix de 2026-10-24: el cortex nunca se asentaba en 3D_SURROUND
 * porque el umbral spatialWidth > 0.9 (sin histéresis) hacía que cualquier
 * micro-fluctuación de telemetría regresara el modo a STEREO_NEUTRAL antes
 * de que el usuario llegara a notarlo. Casos verificados en host con
 * kotlinc 2.0.21 antes de escribir este archivo (8/8 OK) — reproducidos
 * aquí como suite permanente.
 */
class SpatialModeHysteresisTest {

    @Test fun `width tipico por defecto (immersion 0,90 x preferencia 1,2) entra a SURROUND`() {
        // spatialWidth = 0.90 * 1.2 * (1 - fatiga*0.25) ~= 1.08 con fatiga baja
        assertEquals("3D_SURROUND", SpatialModeHysteresis.next(1.08f, "STEREO_NEUTRAL"))
    }

    @Test fun `justo en el umbral de entrada 0,86 ya entra a SURROUND`() {
        assertEquals("3D_SURROUND", SpatialModeHysteresis.next(0.86f, "STEREO_NEUTRAL"))
    }

    @Test fun `una caida leve dentro de la banda de histeresis no regresa a STEREO`() {
        // 0.80 esta entre SURROUND_EXIT(0.75) y SURROUND_ENTER(0.85): se mantiene
        assertEquals("3D_SURROUND", SpatialModeHysteresis.next(0.80f, "3D_SURROUND"))
    }

    @Test fun `una caida real por debajo del umbral de salida si regresa a STEREO`() {
        assertEquals("STEREO_NEUTRAL", SpatialModeHysteresis.next(0.70f, "3D_SURROUND"))
    }

    @Test fun `entra a HOLOGRAM desde SURROUND`() {
        assertEquals("3D_HOLOGRAM", SpatialModeHysteresis.next(1.35f, "3D_SURROUND"))
    }

    @Test fun `HOLOGRAM tambien tiene histeresis antes de bajar a SURROUND`() {
        assertEquals("3D_HOLOGRAM", SpatialModeHysteresis.next(1.25f, "3D_HOLOGRAM"))
        assertEquals("3D_SURROUND", SpatialModeHysteresis.next(1.0f, "3D_HOLOGRAM"))
    }

    @Test fun `caida brusca desde HOLOGRAM puede llegar directo a STEREO`() {
        assertEquals("STEREO_NEUTRAL", SpatialModeHysteresis.next(0.3f, "3D_HOLOGRAM"))
    }

    @Test fun `nunca queda atascado- STEREO con width alto sale de inmediato`() {
        assertEquals("3D_SURROUND", SpatialModeHysteresis.next(0.86f, "STEREO_NEUTRAL"))
        assertEquals("3D_HOLOGRAM", SpatialModeHysteresis.next(1.31f, "STEREO_NEUTRAL"))
    }
}
