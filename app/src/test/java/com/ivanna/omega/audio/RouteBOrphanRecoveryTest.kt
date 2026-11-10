// RouteBOrphanRecoveryTest.kt
// ============================================================================
// PRUEBAS UNITARIAS — Bucle de retroalimentación por beacon (v2.5.386)
// © 2026 Luis Uriel Pimentel Pérez
//
// Cubre la lógica pura del fix de Ruta B (v2.5.386): el efecto omega puede
// quedar en "Orphan Effect Chains" (creado y habilitado, 0 frames) y el
// watchdog anterior NUNCA consultaba el beacon, así que el huérfano quedaba
// para siempre. decideOrphanRecreation decide, solo con el beacon, si hay
// que destruir el efecto y recrearlo en la misma sesión real.
//
// La recreación real (closeSession + re-attach en el watchdog) y la lectura
// del beacon solo se pueden verificar en el teléfono: en logcat
// 'Ruta B HUÉRFANA: efecto acoplado a sesión N sin procesar frames' dice
// que el bucle disparó, y el beacon creciendo después confirma el re-acople.
// ============================================================================

package com.ivanna.omega.audio

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class RouteBOrphanRecoveryTest {

    private val noBaseline = OrphanBeaconState(-1, 0)

    // ── Casos que NO recrean y resetean el estado ──────────────────────────
    @Test fun noAttachedSessionDoesNotRecreate() {
        val d = decideOrphanRecreation(
            attachedSessionId = 0, discoveredSessionId = 249,
            sessionActivelyPlaying = true, beaconFrames = 1000,
            state = OrphanBeaconState(1000, 2)
        )
        assertFalse(d.recreate)
        assertEquals(OrphanBeaconState(-1, 0), d.nextState)
    }

    @Test fun discoveredSessionChangedDoesNotRecreate() {
        // La sesión descubierta cambió (YTM era 249, ahora 297): no es
        // orfandad, es cambio de sesión — lo maneja tryAttachDiscoveredSession.
        val d = decideOrphanRecreation(
            attachedSessionId = 249, discoveredSessionId = 297,
            sessionActivelyPlaying = true, beaconFrames = 1000,
            state = OrphanBeaconState(1000, 2)
        )
        assertFalse(d.recreate)
        assertEquals(OrphanBeaconState(-1, 0), d.nextState)
    }

    @Test fun pausedSessionDoesNotRecreate() {
        // En pausa los frames congelados son lo esperado, no orfandad.
        val d = decideOrphanRecreation(
            attachedSessionId = 249, discoveredSessionId = 249,
            sessionActivelyPlaying = false, beaconFrames = 5000,
            state = OrphanBeaconState(5000, 2)
        )
        assertFalse(d.recreate)
        assertEquals(OrphanBeaconState(-1, 0), d.nextState)
    }

    // ── Beacon no disponible: fail-closed ───────────────────────────────────
    @Test fun unavailableBeaconKeepsStateAndDoesNotRecreate() {
        val before = OrphanBeaconState(1000, 2)
        val d = decideOrphanRecreation(
            attachedSessionId = 249, discoveredSessionId = 249,
            sessionActivelyPlaying = true, beaconFrames = -1,
            state = before
        )
        assertFalse(d.recreate)
        assertEquals(before, d.nextState)
    }

    // ── Línea base y progreso ───────────────────────────────────────────────
    @Test fun firstObservationTakesBaseline() {
        val d = decideOrphanRecreation(
            attachedSessionId = 249, discoveredSessionId = 249,
            sessionActivelyPlaying = true, beaconFrames = 12345,
            state = noBaseline
        )
        assertFalse(d.recreate)
        assertEquals(OrphanBeaconState(12345, 0), d.nextState)
    }

    @Test fun growingFramesResetCycles() {
        val d = decideOrphanRecreation(
            attachedSessionId = 249, discoveredSessionId = 249,
            sessionActivelyPlaying = true, beaconFrames = 2000,
            state = OrphanBeaconState(1000, 2)
        )
        assertFalse(d.recreate)
        assertEquals(OrphanBeaconState(2000, 0), d.nextState)
    }

    @Test fun counterResetAfterEffectRecreationDoesNotRecreate() {
        // El contador bajó: el efecto se recreó (o el beacon se reabrió).
        // No es orfandad: se toma la nueva línea base.
        val d = decideOrphanRecreation(
            attachedSessionId = 249, discoveredSessionId = 249,
            sessionActivelyPlaying = true, beaconFrames = 10,
            state = OrphanBeaconState(999999, 2)
        )
        assertFalse(d.recreate)
        assertEquals(OrphanBeaconState(10, 0), d.nextState)
    }

    // ── Congelado: acumula y recrea a los 3 ciclos ─────────────────────────
    @Test fun frozenFramesAccumulateCycles() {
        val d1 = decideOrphanRecreation(249, 249, true, 5000, OrphanBeaconState(5000, 0))
        assertFalse(d1.recreate)
        assertEquals(OrphanBeaconState(5000, 1), d1.nextState)

        val d2 = decideOrphanRecreation(249, 249, true, 5000, d1.nextState)
        assertFalse(d2.recreate)
        assertEquals(OrphanBeaconState(5000, 2), d2.nextState)
    }

    @Test fun frozenFramesRecreateAtThreeCycles() {
        // 3 ciclos × 3 s = ~9 s congelado mientras suena: huérfano.
        val d = decideOrphanRecreation(249, 249, true, 5000, OrphanBeaconState(5000, 2))
        assertTrue(d.recreate)
        assertEquals(OrphanBeaconState(5000, 3), d.nextState)
    }

    @Test fun frozenFramesKeepRecreatingWhileStuck() {
        val d = decideOrphanRecreation(249, 249, true, 5000, OrphanBeaconState(5000, 5))
        assertTrue(d.recreate)
    }

    @Test fun customThresholdRespected() {
        val d = decideOrphanRecreation(
            249, 249, true, 5000, OrphanBeaconState(5000, 0),
            unchangedCyclesToRecreate = 1
        )
        assertTrue(d.recreate)
    }
}
