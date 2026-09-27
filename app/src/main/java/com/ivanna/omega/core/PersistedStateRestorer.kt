package com.ivanna.omega.core

import android.content.Context
import android.util.Log
import com.ivanna.omega.magisk.OmegaEngineBridge
import com.ivanna.omega.spatial.IvannaSpatialEngine
import com.ivanna.omega.spatial.IvannaSpatialManager
import com.ivanna.omega.ui.AdaptiveControlsPrefs
import com.ivanna.omega.ui.SpatialAudioPrefs
import com.ivanna.omega.saf.SaFBridge
import com.ivanna.omega.saf.SaFRoomBridge

object PersistedStateRestorer {
    private const val TAG = "PersistedStateRestorer"

    fun restore(context: Context) {
        runCatching { com.ivanna.omega.audio.HiResAudioManager.restore(context) }
        val ctx = context.applicationContext

        // 1. Restaurar AdaptiveControlsPrefs
        val acPrefs = AdaptiveControlsPrefs.load(ctx)
        if (IvannaNativeLib.isLoaded) {
            runCatching { IvannaNativeLib.nativeSetEta(acPrefs.nhoEta) }
            runCatching { IvannaNativeLib.nativeSetNPMax(acPrefs.agcTarget / -36f) }
            runCatching { IvannaNativeLib.nativeSetDelta(acPrefs.agcRate) }
            runCatching { IvannaNativeLib.nativeSetHarmonicGain(acPrefs.nhoHarmonicGain) }
            runCatching { IvannaNativeLib.nativeSetBeta(acPrefs.nhoLateralInhib) }
            runCatching { IvannaNativeLib.nativeSetAlpha(acPrefs.nhoOhcGain) }
            
            // Anti-Dolby & Spatial
            runCatching { IvannaNativeLib.nativeInitializeEvolution(acPrefs.evoPopSize, acPrefs.evoGenerations) }
            runCatching { IvannaNativeLib.nativeSetMutationRate(acPrefs.evoMutationRate) }
            runCatching { IvannaNativeLib.nativeSetPhaseParameters(acPrefs.phaseOracleIntensity, acPrefs.phaseOracleIntensity * 0.7f, acPrefs.phaseOracleIntensity * 0.5f) }
            runCatching { IvannaNativeLib.nativeSetAntiDolbyIntensity(acPrefs.antiDolbyThreshold * acPrefs.tinymlInferenceGain.coerceIn(0f, 2f)) }
            runCatching { IvannaNativeLib.nativeSetSpatialWet(acPrefs.spatialSuppression) }
            
            // NPE Controls
            // npeMasterGain is mapped to ETA (wet NHO) via PiLstmBridge in some places, but let's just make sure PiLstmBridge gets the state if it exists.
            runCatching { 
                com.ivanna.omega.neuromorphic.PiLstmBridge.setMasterGain(acPrefs.npeMasterGain)
            }
            
            // Azimuth/Elevation
            val aziRad = acPrefs.binauralAzimuth * Math.PI.toFloat() / 180f
            val eleRad = acPrefs.binauralElevation * Math.PI.toFloat() / 180f
            IvannaSpatialEngine.setAzimuth(aziRad)
            IvannaSpatialEngine.setElevation(eleRad)
            runCatching { IvannaNativeLib.nativeSetSpatialAngleRad(aziRad) }
            
            // Intelligent Upmixing (HOA)
            val paramStore = com.ivanna.omega.core.ParameterStore(ctx)
            runCatching { IvannaNativeLib.nativeSetIntelligentUpmixingEnabled(paramStore.isHoaUpmixingEnabled()) }
            runCatching { IvannaNativeLib.nativeSetUpmixingImmersivity(paramStore.getHoaImmersivity()) }
            runCatching { com.ivanna.omega.magisk.MagiskBridge.sendCommand("{\"action\":\"SET_PERCEPTUAL_STATE\",\"upmixingEnabled\":${if (paramStore.isHoaUpmixingEnabled()) 1.0f else 0.0f},\"upmixingImmersivity\":${paramStore.getHoaImmersivity()}}") }

            // Wave Field Synthesis (WFS) & SPSC Ring Buffer
            runCatching { IvannaNativeLib.nativeSetWfsEnabled(paramStore.isWfsEnabled()) }
            runCatching { IvannaNativeLib.nativeSetWfsSpread(paramStore.getWfsSpread()) }
            runCatching { IvannaNativeLib.nativeSetSpscRingFactor(acPrefs.spscRingFactor) }
            runCatching { com.ivanna.omega.magisk.MagiskBridge.sendCommand("{\"action\":\"SET_SPSC_RING_FACTOR\",\"factor\":${acPrefs.spscRingFactor}}") }
        }

        // 2. Restaurar HarmonicExciterPrefs (perceptual state)
        val hePrefs = ctx.getSharedPreferences("harmonic_exciter_prefs", Context.MODE_PRIVATE)
        val exciterEnabled = hePrefs.getBoolean("exciterEnabled", false)
        val harmonicGain = hePrefs.getFloat("harmonicGain", 0.0f)
        val antiDolby = hePrefs.getFloat("antiDolby", 0.0f)
        
        if (OmegaEngineBridge.isConnected) {
            runCatching {
                OmegaEngineBridge.sendPerceptualState(
                    compressor     = -5.5f,
                    exciterRed     = 0.15f,
                    highCut        = 19500f,
                    spatialWidth   = 1.55f,
                    loudnessTarget = -16.0f,
                    harmonicGain   = if (exciterEnabled) harmonicGain else 0f,
                    antiDolby      = antiDolby
                )
            }.onFailure { Log.w(TAG, "Fallo al restaurar HarmonicExciterPrefs: ${it.message}") }
        }

        // 3. Restaurar SpatialAudioPrefs (HRTF, RIR, SAF — Root y Non-Root)
        val saPrefs = SpatialAudioPrefs.load(ctx)

        // Eje Supremo: Inversión Biomecánica Coclear (Cochlear-PINN)
        runCatching {
            NativeBridge.setCochlearInverseEnabled(saPrefs.cochlearInverseEnabled)
            NativeBridge.setCochlearIntensity(saPrefs.cochlearIntensity)
        }
        
        IvannaSpatialEngine.enabled = saPrefs.hrtfEnabled
        if (saPrefs.hrtfEnabled) {
            IvannaSpatialManager.setHrtfSubject(saPrefs.hrtfSubject)
        }

        // Asegurar dataset RIR extraído y registrado tanto en Non-Root como en Root
        runCatching { OmegaEngineBridge.ensureRirDataset(ctx) }

        if (saPrefs.safEnabled) {
            runCatching { SaFBridge.nativeSaFInit("/data/adb/ivanna_omega/SAF_model.json") }
            val activeRt60 = if (saPrefs.rirEnabled) saPrefs.rirRt60 else 0.34f
            runCatching {
                SaFRoomBridge.optimiseForCurrentRoom(rt60 = activeRt60, drr = 10.31f, steps = 32)
            }
            val q = SaFRoomBridge.getParams()
            val qScaled = FloatArray(7) { i ->
                val base = q.getOrElse(i) { 0f }
                base * saPrefs.safIntensity
            }
            OmegaEngineBridge.pushSafLatentQ(qScaled, gain = saPrefs.safIntensity)
        } else {
            runCatching { SaFBridge.nativeSaFReset() }
            OmegaEngineBridge.pushSafLatentQ(FloatArray(7), gain = 0f)
        }

        if (saPrefs.rirEnabled) {
            OmegaEngineBridge.setRoom(saPrefs.rirRt60, saPrefs.rirWet)
        } else {
            OmegaEngineBridge.disableRoom()
        }

        // 4. Restaurar los 5 Ejes de Supremacía Cuántico-Neuromórfica (C++23 Lock-Free)
        runCatching {
            val supremeState = com.ivanna.omega.ui.SupremeAxesPrefs.load(ctx)
            com.ivanna.omega.ui.SupremeAxesPrefs.applyToNative(supremeState)
        }.onFailure { Log.w(TAG, "Fallo al restaurar SupremeAxesPrefs: ${it.message}") }

        // 5. Restaurar calibración de layout de altavoces WFS (7 altavoces virtuales)
        runCatching {
            com.ivanna.omega.spatial.WfsCalibrationManager.restore(ctx)
        }.onFailure { Log.w(TAG, "Fallo al restaurar WfsCalibrationManager: ${it.message}") }

        Log.i(TAG, "✅ Todos los controles descableados restaurados (Persistencia total conectada)")
    }
}
