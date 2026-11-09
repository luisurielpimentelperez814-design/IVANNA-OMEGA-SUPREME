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
}
