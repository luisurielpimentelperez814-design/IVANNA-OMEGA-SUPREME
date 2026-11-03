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
        assertTrue(FotoHoloRouteB.json(0f).contains("\"spatialWidth\":0.0000"))
    }

    @Test fun activeSpatialOwnersReceiveOnlyTheirOwnParameter() {
        val wfs = FotoHoloRouteB.wfsJson(1.25f)
        assertTrue(wfs.contains("\"action\":\"SET_WFS\""))
        assertTrue(wfs.contains("\"wfsSpread\":1.2500"))
        assertFalse(wfs.contains("wfsEnabled"))

        val hoa = FotoHoloRouteB.upmixJson(0.75f)
        assertTrue(hoa.contains("\"action\":\"SET_UPMIXING\""))
        assertTrue(hoa.contains("\"upmixingImmersivity\":0.7500"))
        assertFalse(hoa.contains("upmixingEnabled"))
    }

    @Test fun spatialOwnerFollowsNativeWfsHoaMsPriority() {
        fun evidence(wfs: Boolean, hoa: Boolean, known: Boolean = true) =
            com.ivanna.omega.magisk.OmegaEngineBridge.ControlPlaneEvidence(
                daemonReachable = true,
                snapshotGeneration = 10L,
                dspAppliedGeneration = 10L,
                processedFrames = 100L,
                effectState = 2,
                status = "dsp_block_applied",
                effectConsumerPresent = true,
                spatialArbitrationKnown = known,
                upmixingEnabled = hoa,
                upmixingImmersivity = 0.75f,
                wfsEnabled = wfs,
                wfsSpread = 1.2f
            )
        assertEquals("WFS", evidence(wfs = true, hoa = true).spatialOwner)
        assertEquals("HOA", evidence(wfs = false, hoa = true).spatialOwner)
        assertEquals("MS", evidence(wfs = false, hoa = false).spatialOwner)
        assertTrue(evidence(wfs = false, hoa = false, known = false).spatialOwner == null)
    }

    @Test fun ownerChangeAndUnknownArbitrationRequireReapplication() {
        assertFalse(FotoHoloRouteB.isDeliveryConfirmed("aplicado por bloque DSP; generación confirmada"))
        assertTrue(FotoHoloRouteB.isDeliveryConfirmed("aplicado por bloque DSP; spread WFS consumido"))
        assertTrue(FotoHoloRouteB.isDeliveryConfirmed("aplicado por bloque DSP; inmersividad HOA consumida"))
        assertTrue(FotoHoloRouteB.isDeliveryConfirmed("aplicado por bloque DSP; anchura M/S activa"))
        assertFalse(FotoHoloRouteB.isDeliveryConfirmed("bloque DSP completado; verificando propietario espacial"))
        assertTrue(FotoHoloRouteB.shouldRetryCommand("propietario cambió; reaplicación necesaria"))
        assertTrue(FotoHoloRouteB.shouldRetryCommand("arbitraje espacial no verificable"))
    }

    @Test fun pendingAndIdleStatesAreNotMistakenForDspDelivery() {
        assertEquals("publicado en Ruta B; esperando generación DSP",
            FotoHoloRouteB.statusFromReply("{\"ok\":true,\"status\":\"awaiting_dsp_generation\",\"generation\":9}"))
        assertEquals("efecto habilitado; esperando bloque de audio",
            FotoHoloRouteB.statusFromReply("{\"ok\":true,\"status\":\"effect_enabled_idle\",\"generation\":9}"))
        assertEquals("aceptado; sin consumidor activo",
            FotoHoloRouteB.statusFromReply("{\"ok\":true,\"status\":\"no_active_effect\",\"generation\":9}"))
    }

    @Test fun onlyDspBlockReadbackConfirmsDeliveryAndPendingStateIsPolled() {
        assertFalse(FotoHoloRouteB.isDeliveryConfirmed("aplicado por bloque DSP; generación confirmada"))
        assertTrue(FotoHoloRouteB.isDeliveryConfirmed("aplicado por bloque DSP; spread WFS consumido"))
        assertTrue(FotoHoloRouteB.isDeliveryConfirmed("aplicado por bloque DSP; inmersividad HOA consumida"))
        assertTrue(FotoHoloRouteB.isDeliveryConfirmed("aplicado por bloque DSP; anchura M/S activa"))
        assertFalse(FotoHoloRouteB.isDeliveryConfirmed("publicado en Ruta B; esperando generación DSP"))
        assertFalse(FotoHoloRouteB.isDeliveryConfirmed("aceptado; sin consumidor activo"))
        assertFalse(FotoHoloRouteB.shouldRetryCommand("aceptado; sin consumidor activo"))
        assertFalse(FotoHoloRouteB.shouldRetryCommand("efecto habilitado; esperando bloque de audio"))
        assertTrue(FotoHoloRouteB.shouldRetryCommand("demonio no disponible"))
        assertTrue(FotoHoloRouteB.shouldRetryCommand("error: IOException"))
    }

    @Test fun responseRequiresExplicitApplicationConfirmation() {
        assertEquals("demonio no disponible", FotoHoloRouteB.statusFromReply(""))
        assertEquals("rechazado por el demonio", FotoHoloRouteB.statusFromReply("{\"ok\":false}"))
        assertEquals("respuesta sin confirmación válida del demonio",
            FotoHoloRouteB.statusFromReply("{\"status\":\"applied\",\"applied\":true}"))
        assertEquals("aceptado; sin consumidor activo",
            FotoHoloRouteB.statusFromReply("{\"ok\":true,\"status\":\"accepted_pending_consumer\"}"))
        assertEquals("publicado en Ruta B; consumidor declarado, audio no verificado",
            FotoHoloRouteB.statusFromReply("{\"ok\":true,\"applied\":true,\"status\":\"applied\",\"consumer\":\"omega_effect\",\"generation\":7}"))
        assertEquals("publicado en Ruta B; sin consumidor DSP declarado",
            FotoHoloRouteB.statusFromReply("{\"ok\":true,\"applied\":true,\"status\":\"applied\",\"consumer\":null,\"generation\":7}"))
        assertEquals("no aplicado: el consumidor DSP no confirmó la aplicación",
            FotoHoloRouteB.statusFromReply("{\"ok\":true,\"command\":\"GET_STATUS\",\"applied\":false,\"status\":\"applied\"}"))
        assertEquals("estado sin confirmación explícita de aplicación",
            FotoHoloRouteB.statusFromReply("{\"ok\":true,\"status\":\"applied\",\"generation\":7}"))
        assertEquals("respuesta sin confirmación válida del demonio", FotoHoloRouteB.statusFromReply("ACK"))
    }

    @Test fun prefixedStatePublishFailureIsStillRetryable() {
        assertTrue(FotoHoloRouteB.shouldRetryCommand("FotoHolo Ruta B: demonio no disponible"))
        assertTrue(FotoHoloRouteB.shouldRetryCommand("FotoHolo Ruta B: error: IOException"))
        assertTrue(FotoHoloRouteB.shouldRetryCommand("FotoHolo Ruta B: respuesta sin confirmación válida del demonio"))
        // Un rechazo explícito del daemon o un estado sin consumidor NO son fallos de transporte.
        assertFalse(FotoHoloRouteB.shouldRetryCommand("FotoHolo Ruta B: rechazado por el demonio"))
        assertFalse(FotoHoloRouteB.shouldRetryCommand("FotoHolo Ruta B: aceptado; sin consumidor activo"))
    }
}
