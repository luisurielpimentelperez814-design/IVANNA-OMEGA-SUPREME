// RouteBSessionDiscoveryTest.kt
// ============================================================================
// PRUEBAS UNITARIAS — Descubrimiento de sesiones de Ruta B (v2.5.375)
// © 2026 Luis Uriel Pimentel Pérez
//
// Cubre la lógica pura del fix de Ruta B (v2.5.375):
//  - isUsableSessionId: solo sessionId > 0 es utilizable. -1 es la copia
//    anonimizada de AOSP (AudioSystem.AUDIO_SESSION_ALLOCATE, lo que devuelve
//    getActivePlaybackConfigurations() sin MODIFY_AUDIO_ROUTING) y 0 es
//    AUDIO_SESSION_NONE. El escáner de la 374 era solo diagnóstico porque
//    no distinguía estos valores; el de la 375 abre las sesiones con ID real
//    y cae al omega global (sesión 0) cuando todas llegan anonimizadas.
//  - usableSessionIds: extrae los IDs utilizables de pares
//    (sessionId, clientUid) reflejados, sin duplicados.
// La reflexión real (getSessionId/getClientUid @hide) y la creación del
// efecto en sesión 0 solo se pueden verificar en el teléfono: en logcat
// 'scanActivePlaybackSessions: N configs, R con sessionId real, A
// anonimizadas' dice si este ROM anonimiza o no, y 'Ruta B GLOBAL activa'
// confirma el fallback.
// ============================================================================

package com.ivanna.omega.audio

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class RouteBSessionDiscoveryTest {

    // ── isUsableSessionId ───────────────────────────────────────────────────
    @Test fun positiveSessionIdsAreUsable() {
        assertTrue(isUsableSessionId(1))
        assertTrue(isUsableSessionId(36081))
        assertTrue(isUsableSessionId(Int.MAX_VALUE))
    }

    @Test fun anonymizedMinusOneIsNotUsable() {
        // AUDIO_SESSION_ALLOCATE (-1): copia anonimizada de AOSP sin
        // MODIFY_AUDIO_ROUTING. Es el valor que el sondeo ve en el Moto G85
        // para TIDAL/YouTube Music mientras la app no sea privilegiada.
        assertFalse(isUsableSessionId(-1))
    }

    @Test fun zeroAndNegativeAreNotUsable() {
        assertFalse(isUsableSessionId(0))   // AUDIO_SESSION_NONE
        assertFalse(isUsableSessionId(-2))
        assertFalse(isUsableSessionId(Int.MIN_VALUE))
    }

    // ── usableSessionIds ────────────────────────────────────────────────────
    @Test fun extractsOnlyUsableIds() {
        val pairs = listOf(36081 to 10123, -1 to -1, 129 to 10456, 0 to 0, -1 to -1)
        assertEquals(listOf(36081, 129), usableSessionIds(pairs))
    }

    @Test fun allAnonymizedYieldsEmpty() {
        val pairs = listOf(-1 to -1, 0 to 0, -1 to -1)
        assertTrue(usableSessionIds(pairs).isEmpty())
    }

    @Test fun emptyInputYieldsEmpty() {
        assertTrue(usableSessionIds(emptyList()).isEmpty())
    }

    @Test fun deduplicatesRepeatedSessionIds() {
        val pairs = listOf(36081 to 10123, 36081 to 10123, 36081 to 10999)
        assertEquals(listOf(36081), usableSessionIds(pairs))
    }

    // ── shouldAttachDiscoveredSession (v2.5.379) ─────────────────────────────
    // Decisión pura: ¿hay que (re)acoplar omega a la sesión descubierta por el
    // daemon root? La sesión 0 es un callejón sin salida (el efecto se crea
    // pero nunca recibe frames) y el sondeo por reflexión no ve sessionIds
    // reales, así que el daemon (root) descubre el ID real por dumpsys.

    @Test fun noDiscoveredSessionMeansNoAttach() {
        // discoveredId 0 = sin música (fail-closed): no se toca nada, sigue sesión 0.
        assertFalse(shouldAttachDiscoveredSession(0, 0, false))
        assertFalse(shouldAttachDiscoveredSession(-1, 0, false))
    }

    @Test fun firstDiscoveredSessionAttaches() {
        assertTrue(shouldAttachDiscoveredSession(14177, 0, false))
    }

    @Test fun alreadyAttachedToSameSessionDoesNothing() {
        assertFalse(shouldAttachDiscoveredSession(14177, 14177, true))
    }

    @Test fun discoveredSessionChangedReattaches() {
        // Los IDs son dinámicos (YTM era 297 y luego 14177): al cambiar hay
        // que liberar la anterior y re-acoplar a la nueva.
        assertTrue(shouldAttachDiscoveredSession(14177, 297, true))
        assertTrue(shouldAttachDiscoveredSession(14177, 297, false))
    }

    @Test fun attachedIdButOmegaDeadReattaches() {
        // Estamos "acoplados" a 14177 pero el omega murió: reintentar.
        assertTrue(shouldAttachDiscoveredSession(14177, 14177, false))
    }
}
