package com.ivanna.omega.core

import android.app.Application
import android.content.Context
import androidx.test.core.app.ApplicationProvider
import com.ivanna.omega.ui.AdaptiveControlsPrefs
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

// Precedencia NPE (v2.5.411, tarea 3b): hay DOS almacenes NPE
// (ivanna_omega_params y ivanna_adaptive_controls) y sus valores difieren
// en el teléfono de Esse. El autoritativo para el motor NPE vivo es
// AdaptiveControlsPrefs (los sliders de la UI solo escriben ahí y el
// restorer restaura desde ahí); los valores NPE de ParameterStore solo
// alimentan el sync al cDSP (OemViewModel). Este test fija que NO hay
// sincronización oculta entre ambos: cada uno conserva lo suyo.
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [33], application = Application::class, manifest = Config.NONE)
class NpeStorePrecedenceTest {
    private lateinit var context: Context

    @Before fun setup() {
        context = ApplicationProvider.getApplicationContext()
        context.getSharedPreferences("ivanna_omega_params", 0).edit().clear().commit()
        context.getSharedPreferences("ivanna_adaptive_controls", 0).edit().clear().commit()
    }

    @Test fun storesAreIndependentNoHiddenSync() {
        val store = ParameterStore(context)
        // Valores distintos en cada almacén (como en el teléfono de Esse).
        store.setNpeNeuroParams(0.60f, 0.13848662f, 0.40f, 2.5f)
        val ac = AdaptiveControlsPrefs.load(context)
        AdaptiveControlsPrefs.save(context, ac.copy(npeLateralInhib = 0.50f))

        // Cada uno conserva lo suyo: escribir en uno no toca el otro.
        assertEquals(0.13848662f, store.getNpeLateralInhib(), 0f)
        assertEquals(0.50f, AdaptiveControlsPrefs.load(context).npeLateralInhib, 0f)
    }

    @Test fun adaptiveControlsHoldsUiDraggedValue() {
        // El valor que arrastra el usuario en el slider NPE vive en
        // AdaptiveControlsPrefs (LaunchedEffect en IvannaControlPanel) y
        // sobrevive a un reload: es la fuente del restorer para el motor vivo.
        val dragged = 0.13848662f
        val ac = AdaptiveControlsPrefs.load(context)
        AdaptiveControlsPrefs.save(context, ac.copy(npeLateralInhib = dragged))
        assertEquals(dragged, AdaptiveControlsPrefs.load(context).npeLateralInhib, 0f)
    }

    @Test fun parameterStoreNpeKeepsSignatureDefaultsWhenFresh() {
        // Sin nada guardado, ParameterStore NPE devuelve los valores del
        // preset de firma (no los de AdaptiveControlsPrefs): son copias
        // independientes, no una vista del otro almacén.
        val store = ParameterStore(context)
        assertEquals(0.60f, store.getNpeHarmonic(), 0f)
        assertEquals(0.50f, store.getNpeLateralInhib(), 0f)
        assertEquals(0.40f, store.getNpeOhcCompression(), 0f)
        assertEquals(2.5f, store.getNpeMasterGain(), 0f)
        assertEquals(0.50f, AdaptiveControlsPrefs.load(context).npeLateralInhib, 0f)
    }
}
