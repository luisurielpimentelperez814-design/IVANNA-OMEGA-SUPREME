// RouteBDiscoveredSessionTest.kt
// ============================================================================
// PRUEBAS UNITARIAS — Sesión descubierta por el daemon root (Ruta B, v2.5.379)
// © 2026 Luis Uriel Pimentel Pérez
//
// Cubre el parseo puro de la respuesta GET_STATUS del daemon a
// ControlPlaneEvidence (OmegaEngineBridge.parseControlPlaneEvidence):
//  - discovered_session_id / discovered_session_gen llegan al modelo.
//  - un daemon viejo (sin esos campos en el JSON) da 0 = sin sesión
//    descubierta (fail-closed: la app sigue con sesión 0 como fallback).
//  - ok=false devuelve null.
//  - los demás campos de evidencia no se rompen con los nuevos campos.
//
// Robolectric porque JSONObject es clase del framework Android.
// La creación del efecto en la sesión real y el crecimiento de effect_frames
// solo se pueden verificar en el teléfono (ver el reporte de la 379).
// ============================================================================

package com.ivanna.omega.magisk

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [33])
class RouteBDiscoveredSessionTest {

    private fun getStatusJson(
        discoveredId: Int? = 14177,
        discoveredGen: Long? = 3L
    ): JSONObject {
        val json = JSONObject().apply {
            put("ok", true)
            put("command", "GET_STATUS")
            put("applied", true)
            put("status", "dsp_block_applied")
            put("generation", 456172L)
            put("route", "SYSTEM_WIDE")
            put("consumer", "omega_effect")
            put("effect_state", 2)
            put("dsp_applied_generation", 456172L)
            put("dsp_frames", 123456L)
            put("intensity", 1.0)
            put("upmixing_enabled", 1.0)
            put("upmixing_immersivity", 1.0)
            put("wfs_enabled", 0.0)
            put("wfs_spread", 1.0)
        }
        if (discoveredId != null) json.put("discovered_session_id", discoveredId)
        if (discoveredGen != null) json.put("discovered_session_gen", discoveredGen)
        return json
    }

    @Test fun parsesDiscoveredSessionFromGetStatus() {
        val evidence = OmegaEngineBridge.parseControlPlaneEvidence(getStatusJson())!!
        assertEquals(14177, evidence.discoveredSessionId)
        assertEquals(3L, evidence.discoveredSessionGen)
        // Los demás campos no se rompen.
        assertEquals(456172L, evidence.snapshotGeneration)
        assertEquals(123456L, evidence.processedFrames)
        assertEquals(2, evidence.effectState)
        assertTrue(evidence.effectConsumerPresent)
        assertTrue(evidence.dspHasAppliedCurrentSnapshot)
    }

    @Test fun oldDaemonWithoutDiscoveredFieldsDefaultsToZero() {
        val evidence = OmegaEngineBridge.parseControlPlaneEvidence(
            getStatusJson(discoveredId = null, discoveredGen = null)
        )!!
        assertEquals(0, evidence.discoveredSessionId)
        assertEquals(0L, evidence.discoveredSessionGen)
        assertTrue(evidence.daemonReachable)
    }

    @Test fun noMusicDiscoveredIsZero() {
        val evidence = OmegaEngineBridge.parseControlPlaneEvidence(
            getStatusJson(discoveredId = 0, discoveredGen = 0L)
        )!!
        assertEquals(0, evidence.discoveredSessionId)
        assertEquals(0L, evidence.discoveredSessionGen)
    }

    @Test fun okFalseReturnsNull() {
        val json = JSONObject().apply { put("ok", false) }
        assertNull(OmegaEngineBridge.parseControlPlaneEvidence(json))
    }

    @Test fun sessionGenIncrementsAreVisible() {
        // El daemon incrementa gen solo cuando la sesión cambia (0->N, N->M,
        // N->0). La app lo usa para detectar el cambio de sesión.
        val first = OmegaEngineBridge.parseControlPlaneEvidence(
            getStatusJson(discoveredId = 297, discoveredGen = 1L)
        )!!
        val second = OmegaEngineBridge.parseControlPlaneEvidence(
            getStatusJson(discoveredId = 14177, discoveredGen = 2L)
        )!!
        assertEquals(297, first.discoveredSessionId)
        assertEquals(1L, first.discoveredSessionGen)
        assertEquals(14177, second.discoveredSessionId)
        assertEquals(2L, second.discoveredSessionGen)
    }
}
