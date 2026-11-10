// ParameterStoreMigration386Test.kt
// ============================================================================
// PRUEBAS UNITARIAS — Migración del sobre 380 (v2.5.386)
// © 2026 Luis Uriel Pimentel Pérez
//
// Cubre la lógica pura de la migración única de ParameterStore (v2.5.386):
// la 384 subió HOLOS a amount 0.75 / width 1.25 / diffuse 0.65 y ORBE spread
// a 1.2; esos valores quedaron PERSISTIDOS y el revert de la 385 solo cambió
// los defaults — PersistedStateRestorer seguía reenviando los valores 384 al
// daemon en cada arranque. clampTo380Envelope los clampa al sobre 380 (los
// valores que sonaban bien en la v2.5.380).
//
// La migración real (lectura/escritura de SharedPreferences + flag de
// una-sola-vez) solo se puede verificar en el teléfono: en logcat
// 'Migración 386: sobre 380 aplicado (holos 0.75->0.5, ...)' confirma que
// los valores 384 se clamparon antes del primer SET_PERCEPTUAL_STATE.
// ============================================================================

package com.ivanna.omega.core

import org.junit.Assert.assertEquals
import org.junit.Test

class ParameterStoreMigration386Test {

    private fun env(
        holosAmount: Float, holosWidth: Float, holosDiffuse: Float, orbeSpread: Float
    ) = ParameterStore.Envelope380(holosAmount, holosWidth, holosDiffuse, orbeSpread)

    @Test fun values384AreClampedTo380Envelope() {
        val out = ParameterStore.clampTo380Envelope(env(0.75f, 1.25f, 0.65f, 1.2f))
        assertEquals(ParameterStore.Envelope380(0.5f, 1.0f, 0.5f, 1.0f), out)
    }

    @Test fun values380PassThroughUnchanged() {
        val out = ParameterStore.clampTo380Envelope(env(0.5f, 1.0f, 0.5f, 1.0f))
        assertEquals(ParameterStore.Envelope380(0.5f, 1.0f, 0.5f, 1.0f), out)
    }

    @Test fun belowEnvelopeValuesPassThroughUnchanged() {
        val out = ParameterStore.clampTo380Envelope(env(0.3f, 0.8f, 0.2f, 0.7f))
        assertEquals(ParameterStore.Envelope380(0.3f, 0.8f, 0.2f, 0.7f), out)
    }

    @Test fun zeroStaysZero() {
        // amount 0 = bypass: la migración no debe encender nada.
        val out = ParameterStore.clampTo380Envelope(env(0f, 0f, 0f, 0f))
        assertEquals(ParameterStore.Envelope380(0f, 0f, 0f, 0f), out)
    }

    @Test fun negativeValuesClampToZero() {
        val out = ParameterStore.clampTo380Envelope(env(-0.5f, -1f, -0.2f, -0.1f))
        assertEquals(ParameterStore.Envelope380(0f, 0f, 0f, 0f), out)
    }

    @Test fun nanFallsBackTo380Defaults() {
        val out = ParameterStore.clampTo380Envelope(env(Float.NaN, Float.NaN, Float.NaN, Float.NaN))
        assertEquals(ParameterStore.Envelope380(0.5f, 1.0f, 0.5f, 1.0f), out)
    }

    @Test fun mixedValuesClampOnlyTheOffenders() {
        val out = ParameterStore.clampTo380Envelope(env(0.5f, 1.5f, 0.4f, 1.0f))
        assertEquals(ParameterStore.Envelope380(0.5f, 1.0f, 0.4f, 1.0f), out)
    }
}
