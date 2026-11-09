// RouteBRecoveryTest.kt
// ============================================================================
// PRUEBAS UNITARIAS — Recuperación de Ruta B (daemon / efecto system-wide)
// © 2026 Luis Uriel Pimentel Pérez
//
// Cubre la lógica pura del fix de Ruta B (v2.5.369, docs/RUTA_B_DAEMON_FIX.md):
//  - OmegaAttachBackoff: reintento de creación del efecto con backoff.
//  - omegaRootCauseMessage: desenrolla la causa real del fallo del constructor.
//  - AudioBackendSelector.isRouteBConfirmed / decideMode: el handshake que
//    impide quedarse en ROOT_DAEMON sin consumidor y detener el fallback
//    local antes de confirmar que Ruta B procesa.
// La creación real del efecto y el socket del daemon solo se pueden verificar
// en el teléfono (ver el doc); aquí se prueba la decisión, no el hardware.
// ============================================================================

package com.ivanna.omega.audio

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class RouteBRecoveryTest {

    // ── Backoff de reintento de attach Omega ────────────────────────────────
    @Test fun backoffStartsShortThenGrows() {
        assertEquals(2_000L, OmegaAttachBackoff.delayMs(0))
        assertEquals(5_000L, OmegaAttachBackoff.delayMs(1))
        assertEquals(10_000L, OmegaAttachBackoff.delayMs(2))
        assertEquals(20_000L, OmegaAttachBackoff.delayMs(3))
        assertEquals(30_000L, OmegaAttachBackoff.delayMs(4))
    }

    @Test fun backoffIsCappedAt30s() {
        assertEquals(30_000L, OmegaAttachBackoff.delayMs(5))
        assertEquals(30_000L, OmegaAttachBackoff.delayMs(100))
        assertEquals(30_000L, OmegaAttachBackoff.delayMs(Int.MAX_VALUE))
    }

    @Test fun backoffIsMonotonicAndNeverZero() {
        var prev = 0L
        for (attempt in 0..20) {
            val d = OmegaAttachBackoff.delayMs(attempt)
            assertTrue("delay($attempt)=$d debe ser > 0", d > 0)
            assertTrue("delay debe ser no-decreciente", d >= prev)
            prev = d
        }
    }

    @Test fun backoffNegativeAttemptCoercesToFirst() {
        assertEquals(2_000L, OmegaAttachBackoff.delayMs(-1))
    }

    // ── Causa raíz del fallo del constructor ────────────────────────────────
    @Test fun rootCauseUnwrapsInvocationTargetException() {
        // El constructor AudioEffect por reflection envuelve el error real en
        // InvocationTargetException con mensaje null. Antes se logueaba solo
        // it.message ("null") y el diagnóstico no veía la causa.
        val real = IllegalArgumentException("UUID de efecto no registrado")
        val wrapped = java.lang.reflect.InvocationTargetException(real)
        val msg = omegaRootCauseMessage(wrapped)
        assertTrue("debe nombrar la causa real: $msg", msg.contains("IllegalArgumentException"))
        assertTrue("debe incluir el mensaje real: $msg", msg.contains("UUID de efecto no registrado"))
    }

    @Test fun rootCauseHandlesNullMessage() {
        val t = RuntimeException()
        val msg = omegaRootCauseMessage(t)
        assertTrue("debe indicar que no hay mensaje: $msg", msg.contains("(sin mensaje)"))
    }

    @Test fun rootCauseKeepsSimpleException() {
        val msg = omegaRootCauseMessage(IllegalStateException("efecto muerto"))
        assertTrue(msg.contains("IllegalStateException"))
        assertTrue(msg.contains("efecto muerto"))
    }

    // ── Confirmación de Ruta B (beacon) ─────────────────────────────────────
    @Test fun routeBConfirmedOnlyWhenEffectInsertedOrProcessing() {
        val sel = AudioBackendSelector
        assertTrue(sel.isRouteBConfirmed(com.ivanna.omega.dsp.DSPBridge.EffectState.ENABLED_IDLE))
        assertTrue(sel.isRouteBConfirmed(com.ivanna.omega.dsp.DSPBridge.EffectState.PROCESSING))
        assertFalse(sel.isRouteBConfirmed(com.ivanna.omega.dsp.DSPBridge.EffectState.NO_EFFECT))
        assertFalse(sel.isRouteBConfirmed(com.ivanna.omega.dsp.DSPBridge.EffectState.UNAVAILABLE))
    }

    // ── Decisión de modo (handshake) ────────────────────────────────────────
    @Test fun decideModeRequiresRootDaemonAndRouteB() {
        val sel = AudioBackendSelector
        // Solo con las tres se entra en ROOT_DAEMON.
        assertEquals(AudioBackendSelector.Mode.ROOT_DAEMON, sel.decideMode(root = true, daemon = true, routeBConfirmed = true))
        // Daemon vivo pero Ruta B sin confirmar -> fallback sigue (ROOT_NO_DAEMON).
        // Este es el bug del 07: antes era ROOT_DAEMON solo por "daemon vivo".
        assertEquals(AudioBackendSelector.Mode.ROOT_NO_DAEMON, sel.decideMode(root = true, daemon = true, routeBConfirmed = false))
        // Root sin daemon.
        assertEquals(AudioBackendSelector.Mode.ROOT_NO_DAEMON, sel.decideMode(root = true, daemon = false, routeBConfirmed = false))
        assertEquals(AudioBackendSelector.Mode.ROOT_NO_DAEMON, sel.decideMode(root = true, daemon = false, routeBConfirmed = true))
        // Sin root.
        assertEquals(AudioBackendSelector.Mode.NO_ROOT, sel.decideMode(root = false, daemon = false, routeBConfirmed = false))
        assertEquals(AudioBackendSelector.Mode.NO_ROOT, sel.decideMode(root = false, daemon = true, routeBConfirmed = true))
    }

    // ── Banner honesto (RouteCapabilities) ──────────────────────────────────
    @Test fun routeCapabilitiesDistinguishesDaemonAlive() {
        // Daemon vivo pero efecto sin confirmar: el título no debe decir que
        // el módulo "no responde" (sí responde; lo que falta es el efecto).
        val withDaemon = RouteCapabilities.limitsFor(AudioBackendSelector.Mode.ROOT_NO_DAEMON, daemonAlive = true)
        assertTrue(withDaemon != null)
        assertTrue(withDaemon!!.first.contains("Ruta B sin confirmar"))
        // Daemon caído: el título original sigue siendo correcto.
        val withoutDaemon = RouteCapabilities.limitsFor(AudioBackendSelector.Mode.ROOT_NO_DAEMON, daemonAlive = false)
        assertTrue(withoutDaemon != null)
        assertTrue(withoutDaemon!!.first.contains("no responde"))
        // ROOT_DAEMON: sin aviso.
        assertEquals(null, RouteCapabilities.limitsFor(AudioBackendSelector.Mode.ROOT_DAEMON, daemonAlive = true))
    }
}
