package com.ivanna.omega.fotoholo

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class FotoHoloRouteBTest {
    @Test fun commandContainsOnlyWidth() {
        val j = FotoHoloRouteB.json(1.2f)
        assertTrue(j.contains("\"action\":\"SET_PERCEPTUAL_STATE\""))
        assertTrue(j.contains("\"spatialWidth\":1.2000"))
        assertFalse(j.contains("compressor") || j.contains("exciter") || j.contains("harmonic"))
    }

    @Test fun responseRequiresExplicitApplicationConfirmation() {
        assertEquals("demonio no disponible", FotoHoloRouteB.statusFromReply(""))
        assertEquals("rechazado por el demonio", FotoHoloRouteB.statusFromReply("{\"ok\":false}"))
        assertEquals("aceptado; sin consumidor activo",
            FotoHoloRouteB.statusFromReply("{\"ok\":true,\"status\":\"accepted_pending_consumer\"}"))
        assertEquals("publicado en Ruta B; consumidor declarado, audio no verificado",
            FotoHoloRouteB.statusFromReply("{\"ok\":true,\"applied\":true,\"status\":\"applied\",\"consumer\":\"omega_effect\",\"generation\":7}"))
        assertEquals("publicado en Ruta B; sin consumidor DSP declarado",
            FotoHoloRouteB.statusFromReply("{\"ok\":true,\"applied\":true,\"status\":\"applied\",\"consumer\":null,\"generation\":7}"))
            FotoHoloRouteB.statusFromReply("{\"ok\":true,\"applied\":true,\"status\":\"applied\",\"generation\":7}"))
        assertEquals("no aplicado: el consumidor DSP no confirmó la aplicación",
            FotoHoloRouteB.statusFromReply("{\"ok\":true,\"command\":\"GET_STATUS\",\"applied\":false,\"status\":\"applied\"}"))
        assertEquals("estado sin confirmación explícita de aplicación",
            FotoHoloRouteB.statusFromReply("{\"ok\":true,\"status\":\"applied\",\"generation\":7}"))
        assertEquals("respuesta no reconocida", FotoHoloRouteB.statusFromReply("ACK"))
    }
}
