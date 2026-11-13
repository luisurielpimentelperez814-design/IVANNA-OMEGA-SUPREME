package com.ivanna.omega.core

import android.app.Application
import android.content.Context
import androidx.test.core.app.ApplicationProvider
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

// Perfiles de fábrica (v2.5.411, tarea 2): una sola fuente de verdad.
// El perfil de arranque es UNA constante (BOOT_PROFILE = Espacial);
// las preferencias existentes no se migran ni se borran.
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [33], application = Application::class, manifest = Config.NONE)
class FactoryProfileTest {
    private lateinit var context: Context

    @Before fun setup() {
        context = ApplicationProvider.getApplicationContext()
        context.getSharedPreferences("ivanna_omega_params", 0).edit().clear().commit()
    }

    @Test fun bootProfileIsEspacial() {
        assertEquals("Espacial", ParameterStore.BOOT_PROFILE.name)
        assertSame(ParameterStore.PROFILE_ESPACIAL, ParameterStore.BOOT_PROFILE)
    }

    @Test fun limpioIsHoaHrtfOnly() {
        val p = ParameterStore.PROFILE_LIMPIO
        assertEquals(0f, p.orbeAmount, 0f)
        assertEquals(0f, p.holosAmount, 0f)
        assertEquals(0f, p.omniAmount, 0f)
        assertEquals(0f, p.hyperAmount, 0f)
        assertFalse(p.volterraEnabled)
    }

    @Test fun espacialAddsOrbeAndHolosOnly() {
        val p = ParameterStore.PROFILE_ESPACIAL
        // v2.5.437 — directiva ORBE 1.0 / HOLOS 0.5 (antes ORBE 0.5).
        assertEquals(1.0f, p.orbeAmount, 0f)
        assertEquals(0.5f, p.holosAmount, 0f)
        assertEquals(0.0f, p.holosHarmonics, 0f)
        assertEquals(0f, p.omniAmount, 0f)
        assertEquals(0f, p.hyperAmount, 0f)
        assertFalse(p.volterraEnabled)
    }

    @Test fun completoMatchesReadmeDefaults() {
        val p = ParameterStore.PROFILE_COMPLETO
        assertEquals(1.0f, p.orbeAmount, 0f)
        assertEquals(0.5f, p.holosAmount, 0f)
        assertEquals(0.0f, p.holosHarmonics, 0f)
        assertEquals(1.0f, p.omniAmount, 0f)
        assertEquals(0.5f, p.hyperAmount, 0f)
        assertTrue(p.volterraEnabled)
        // Valores de partida (tarea 2): EQ HOLOS plano, distance 0.35 m,
        // harmonics 0, hyper_bass_trim 0, Harman 2018 (targetKind 0).
        assertEquals(0f, p.holosEqLow, 0f)
        assertEquals(0f, p.holosEqMid, 0f)
        assertEquals(0f, p.holosEqHigh, 0f)
        assertEquals(0f, p.holosEqPresence, 0f)
        assertEquals(0f, p.holosEqTiltDb, 0f)
        assertEquals(0.35f, p.holosDistanceM, 0f)
        assertEquals(0.35f, p.orbeDistanceM, 0f)
        assertEquals(0f, p.hyperBassTrimDb, 0f)
        assertEquals(0, p.hyperTargetKind)
    }

    @Test fun freshStoreReturnsBootProfileDefaults() {
        val store = ParameterStore(context)
        // v2.5.437 — BOOT_PROFILE = PROFILE_ESPACIAL, ORBE 1.0 (antes 0.5).
        assertEquals(1.0f, store.getOrbeAmount(), 0f)
        assertEquals(0.5f, store.getHolosAmount(), 0f)
        assertEquals(0.0f, store.getHolosHarmonics(), 0f)
        assertEquals(0f, store.getOmniAmount(), 0f)
        assertEquals(0f, store.getHyperAmount(), 0f)
        assertFalse(store.isVolterraEnabled())
        assertEquals(0f, store.getHolosEqLow(), 0f)
        assertEquals(0, store.getHyperTargetKind())
    }

    @Test fun storedPrefsWinOverProfile() {
        val store = ParameterStore(context)
        // Valores vivos de Esse: se respetan, no los pisa el perfil.
        store.setHolosParams(0.359f, 0.35f, 1.0f, 3.0f, 0.5f, 0.298f, 0.5f, 1.0f, 1.0f, 1.0f, 0.34f, true, 24)
        store.setHolosEq(7.8f, -8.1f, -8.6f, 0f, 5.2f, 1000f)
        assertEquals(0.359f, store.getHolosAmount(), 0f)
        assertEquals(0.298f, store.getHolosHarmonics(), 0f)
        assertEquals(7.8f, store.getHolosEqLow(), 0f)
        assertEquals(-8.1f, store.getHolosEqMid(), 0f)
    }
}
