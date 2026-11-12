package com.ivanna.omega.audio

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

/**
 * Mezcla de captura AJUSTABLE (v2.5.412): el slider "MEZCLA CAPTURA"
 * persiste en `ivanna_capture` y el engine lo re-lee throttled. Default
 * 0.60 = DELTA_BLEND_SOLO (el valor vivo actual: sin cambio de arranque).
 * Rango 0.20–1.00; fuera de rango se clamp; no-finito → default.
 */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [33])
class CaptureMixGainPrefsTest {

    private lateinit var context: Context

    @Before
    fun setup() {
        context = ApplicationProvider.getApplicationContext()
        context.getSharedPreferences(
            PlaybackCaptureService.CAPTURE_MIX_PREFS, Context.MODE_PRIVATE
        ).edit().clear().commit()
    }

    @Test fun defaultIsCurrentLiveValue() {
        assertEquals(0.60f, PlaybackCaptureService.getCaptureMixGain(context), 0.0f)
    }

    @Test fun roundTripPersists() {
        PlaybackCaptureService.setCaptureMixGain(context, 0.80f)
        assertEquals(0.80f, PlaybackCaptureService.getCaptureMixGain(context), 0.0f)
        PlaybackCaptureService.setCaptureMixGain(context, 1.00f)
        assertEquals(1.00f, PlaybackCaptureService.getCaptureMixGain(context), 0.0f)
        PlaybackCaptureService.setCaptureMixGain(context, 0.20f)
        assertEquals(0.20f, PlaybackCaptureService.getCaptureMixGain(context), 0.0f)
    }

    @Test fun outOfRangeIsClamped() {
        PlaybackCaptureService.setCaptureMixGain(context, 1.50f)
        assertEquals(1.00f, PlaybackCaptureService.getCaptureMixGain(context), 0.0f)
        PlaybackCaptureService.setCaptureMixGain(context, 0.05f)
        assertEquals(0.20f, PlaybackCaptureService.getCaptureMixGain(context), 0.0f)
    }

    @Test fun nonFiniteFallsBackToDefault() {
        PlaybackCaptureService.setCaptureMixGain(context, Float.NaN)
        assertEquals(0.60f, PlaybackCaptureService.getCaptureMixGain(context), 0.0f)
        PlaybackCaptureService.setCaptureMixGain(context, Float.POSITIVE_INFINITY)
        assertEquals(0.60f, PlaybackCaptureService.getCaptureMixGain(context), 0.0f)
    }

    @Test fun clampIsPure() {
        assertEquals(0.20f, PlaybackCaptureService.clampCaptureMixGain(0.10f), 0.0f)
        assertEquals(1.00f, PlaybackCaptureService.clampCaptureMixGain(1.50f), 0.0f)
        assertEquals(0.60f, PlaybackCaptureService.clampCaptureMixGain(0.60f), 0.0f)
        assertEquals(0.60f, PlaybackCaptureService.clampCaptureMixGain(Float.NaN), 0.0f)
    }
}
