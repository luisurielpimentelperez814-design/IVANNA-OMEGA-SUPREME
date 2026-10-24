package com.ivanna.omega.fotoholo

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class FotoHoloRouteBTest {
    @Test fun \`el comando lleva solo el ancho para no pisar el resto del estado del demonio\`() {
        val j = FotoHoloRouteB.json(1.2f)
        assertTrue(j.contains("\"action\":\"SET_PERCEPTUAL_STATE\""))
        assertTrue(j.contains("\"spatialWidth\":1.2000"))
        assertFalse(j.contains("compressor") || j.contains("exciter") || j.contains("harmonic"))
    }

    @Test fun \`la respuesta del demonio se traduce sin asumir exito\`() {
        assertEquals("demonio no disponible", FotoHoloRouteB.statusFromReply(""))
        assertEquals("rechazado por el demonio", FotoHoloRouteB.statusFromReply("{\"ok\":false}"))
        assertEquals("aceptado; sin consumidor activo",
            FotoHoloRouteB.statusFromReply("{\"ok\":true,\"status\":\"accepted_pending_consumer\"}"))
        assertEquals("aplicado en Ruta B",
            FotoHoloRouteB.statusFromReply("{\"ok\":true,\"applied\":true,\"status\":\"applied\",\"generation\":7}"))
        assertEquals("no aplicado: el consumidor DSP no confirmó la aplicación",
            FotoHoloRouteB.statusFromReply("{\"ok\":true,\"command\":\"GET_STATUS\",\"applied\":false,\"status\":\"applied\"}"))
        assertEquals("estado sin confirmación explícita de aplicación",
            FotoHoloRouteB.statusFromReply("{\"ok\":true,\"status\":\"applied\",\"generation\":7}"))
        assertEquals("respuesta no reconocida", FotoHoloRouteB.statusFromReply("ACK"))
    }
}
