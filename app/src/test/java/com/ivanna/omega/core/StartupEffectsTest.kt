package com.ivanna.omega.core

import android.app.Application
import android.content.Context
import androidx.test.core.app.ApplicationProvider
import com.ivanna.omega.audio.EffectProfileStore
import com.ivanna.omega.audio.IvannaEffectProfile
import com.ivanna.omega.audio.IvannaGlobalEffectManager
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [33], application = Application::class, manifest = Config.NONE)
class StartupEffectsTest {
    private lateinit var context: Context
    @Before fun setup() {
        context = ApplicationProvider.getApplicationContext()
        context.getSharedPreferences("ivanna_omega_params", 0).edit().clear().commit()
        context.getSharedPreferences("ivanna_global_effect_profile", 0).edit().clear().commit()
        context.getSharedPreferences("ivanna_iso226", 0).edit().clear().commit()
    }
    @Test fun firstLaunchSeedsSignatureWithoutClaimingAcousticCalibration() {
        val store = ParameterStore(context)
        assertTrue(store.applySignaturePresetIfFirstRun())
        assertFalse(store.loadIso226Calibrated())
        assertEquals("IVANNA OMEGA", store.getCurrentPreset())
        store.setWidth(.8f)
        assertFalse(store.applySignaturePresetIfFirstRun())
        assertEquals(.8f, store.getWidth(), 0f)
    }
    @Test fun upgradeWithoutLegacyFlagPreservesUserAndMeasuredIsoSettings() {
        val store = ParameterStore(context)
        store.setWidth(.6f)
        store.saveIso226(65f, 80f, FloatArray(10) { 1f })
        assertFalse(store.applySignaturePresetIfFirstRun())
        assertEquals(.6f, store.getWidth(), 0f)
        assertTrue(store.loadIso226Calibrated())
    }
    @Test fun nativeSampleRateMetadataDoesNotSuppressFirstLaunchSignature() {
        context.getSharedPreferences("ivanna_omega_params", 0).edit().putInt("native_sample_rate_hz", 96000).commit()
        assertTrue(ParameterStore(context).applySignaturePresetIfFirstRun())
        assertEquals(96000, ParameterStore(context).loadNativeSampleRate())
    }
    @Test fun restoringMissingIsoDoesNotSeedOrApplyAnUnmeasuredCurve() {
        val manager = IvannaGlobalEffectManager(context)
        assertFalse(com.ivanna.omega.audio.Iso226Calibrator.restoreIfSaved(context, manager))
        assertFalse(context.getSharedPreferences("ivanna_iso226", 0).contains("calibrated"))
        assertArrayEquals(IvannaEffectProfile.IVANNA_OMEGA.eqBands, manager.currentProfile().eqBands)
    }
    @Test fun restartRestoresCustomBaseProfileAndIsoDoesNotReplaceIt() {
        val custom = IvannaEffectProfile.WARM.copy(compRatio = 1.8f)
        val first = IvannaGlobalEffectManager(context)
        first.applyProfile(custom)
        first.applyIso226Compensation(FloatArray(10) { 2f })
        first.applyIso226Compensation(FloatArray(10) { 2f })
        assertArrayEquals(custom.eqBands, first.currentProfile().eqBands)
        val restored = IvannaGlobalEffectManager(context).currentProfile()
        assertArrayEquals(custom.eqBands, restored.eqBands)
        assertEquals(custom.compRatio, restored.compRatio, 0f)
        assertNotNull(EffectProfileStore.load(context))
    }
}
