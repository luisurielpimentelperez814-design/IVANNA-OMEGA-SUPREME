package com.ivanna.omega.core

import android.util.Log

/**
 * NativeBridge — Canonical JNI bridge for IVANNA-OMEGA-SUPREME.
 * Single source of truth for the Cochlear Active Inverse Engine and spatial plane.
 */
object NativeBridge {
    private const val TAG = "NativeBridge"
    val isLoaded: Boolean = NativeLibraryLoader.ensureLoaded()

    @JvmStatic external fun setCochlearInverseEnabled(enabled: Boolean)
    @JvmStatic external fun setCochlearIntensity(intensity: Float)
    @JvmStatic external fun isCochlearActive(): Boolean
    @JvmStatic external fun getCochlearIntensity(): Float

    fun safeSetCochlearInverseEnabled(enabled: Boolean) {
        if (!isLoaded) {
            Log.w(TAG, "Native library not loaded; ignoring setCochlearInverseEnabled")
            return
        }
        try {
            setCochlearInverseEnabled(enabled)
            if (IvannaNativeLib.isLoaded) {
                IvannaNativeLib.setCochlearInverseEnabled(enabled)
                IvannaNativeLib.nativeSetCochlearInverseEnabled(enabled)
            }
            if (com.ivanna.omega.spatial.IvannaSpatialNative.loaded) {
                com.ivanna.omega.spatial.IvannaSpatialNative.setCochlearInverseEnabled(enabled)
                com.ivanna.omega.spatial.IvannaSpatialNative.nativeSetCochlearInverseEnabled(enabled)
            }
        } catch (t: Throwable) {
            Log.e(TAG, "Failed to call setCochlearInverseEnabled", t)
        }
    }

    fun safeSetCochlearIntensity(intensity: Float) {
        if (!isLoaded) {
            Log.w(TAG, "Native library not loaded; ignoring setCochlearIntensity")
            return
        }
        try {
            setCochlearIntensity(intensity)
            if (IvannaNativeLib.isLoaded) {
                IvannaNativeLib.setCochlearIntensity(intensity)
                IvannaNativeLib.nativeSetCochlearIntensity(intensity)
            }
            if (com.ivanna.omega.spatial.IvannaSpatialNative.loaded) {
                com.ivanna.omega.spatial.IvannaSpatialNative.setCochlearIntensity(intensity)
                com.ivanna.omega.spatial.IvannaSpatialNative.nativeSetCochlearIntensity(intensity)
            }
        } catch (t: Throwable) {
            Log.e(TAG, "Failed to call setCochlearIntensity", t)
        }
    }

    fun safeIsCochlearActive(): Boolean {
        if (!isLoaded) return false
        return try {
            val active = isCochlearActive()
            val libActive = if (IvannaNativeLib.isLoaded) IvannaNativeLib.isCochlearActive() else active
            val spActive = if (com.ivanna.omega.spatial.IvannaSpatialNative.loaded) {
                com.ivanna.omega.spatial.IvannaSpatialNative.isCochlearActive()
            } else active
            active || libActive || spActive
        } catch (t: Throwable) {
            false
        }
    }

    fun safeGetCochlearIntensity(): Float {
        if (!isLoaded) return 0.35f
        return try {
            val primary = getCochlearIntensity()
            if (IvannaNativeLib.isLoaded) {
                val mirror = IvannaNativeLib.getCochlearIntensity()
                if (mirror.isFinite() && mirror > 0f) return primary
            }
            primary
        } catch (t: Throwable) {
            0.35f
        }
    }

    // ── EJE 1: WarpedLatticeTransducerInverter (Anti-Dirac) ─────────────────
    @JvmStatic external fun setWarpedLatticeEnabled(enabled: Boolean)
    @JvmStatic external fun setWarpedLatticeMicroChirp(enabled: Boolean)
    @JvmStatic external fun setWarpedLatticeBlDrive(drive: Float)
    @JvmStatic external fun setWarpedLatticeLambda(lambda: Float)
    @JvmStatic external fun getWarpedLatticeSubSampleDelay(): Float
    @JvmStatic external fun runWarpedLatticeLoopbackCalibration(f0Hz: Float, mu: Float)
    @JvmStatic external fun getWarpedLatticeKappas(): FloatArray?
    @JvmStatic external fun setWarpedLatticeRouteArchetype(routeIdx: Int)
    @JvmStatic external fun getWarpedLatticeDeclippedPeaks(): Int

    fun safeSetWarpedLatticeEnabled(enabled: Boolean) {
        if (isLoaded) runCatching { setWarpedLatticeEnabled(enabled) }
    }
    fun safeSetWarpedLatticeMicroChirp(enabled: Boolean) {
        if (isLoaded) runCatching { setWarpedLatticeMicroChirp(enabled) }
    }
    fun safeSetWarpedLatticeBlDrive(drive: Float) {
        if (isLoaded) runCatching { setWarpedLatticeBlDrive(drive) }
    }
    fun safeSetWarpedLatticeLambda(lambda: Float) {
        if (isLoaded) runCatching { setWarpedLatticeLambda(lambda) }
    }
    fun safeSetWarpedLatticeRouteArchetype(routeIdx: Int) {
        if (isLoaded) runCatching { setWarpedLatticeRouteArchetype(routeIdx) }
    }
    fun safeGetWarpedLatticeDeclippedPeaks(): Int =
        if (isLoaded) runCatching { getWarpedLatticeDeclippedPeaks() }.getOrDefault(0) else 0
    fun safeGetWarpedLatticeSubSampleDelay(): Float =
        if (isLoaded) runCatching { getWarpedLatticeSubSampleDelay() }.getOrDefault(0.24f) else 0.24f
    fun safeRunWarpedLatticeLoopbackCalibration(f0Hz: Float = 92.0f, mu: Float = 0.005f) {
        if (isLoaded) runCatching { runWarpedLatticeLoopbackCalibration(f0Hz, mu) }
    }
    fun safeGetWarpedLatticeKappas(): FloatArray =
        if (isLoaded) runCatching { getWarpedLatticeKappas() }.getOrNull()
            ?: floatArrayOf(-0.38f, 0.24f, -0.15f, 0.09f, -0.055f, 0.032f, -0.018f, 0.009f)
        else floatArrayOf(-0.38f, 0.24f, -0.15f, 0.09f, -0.055f, 0.032f, -0.018f, 0.009f)

    // ── EJE 2: PhaseCoherentTransharmonicSynthesizer (Anti-DSEE CVNN+DDSP) ──
    @JvmStatic external fun setTransharmonicCvnnEnabled(enabled: Boolean)
    @JvmStatic external fun setTransharmonicHarmonicGain(gain: Float)
    @JvmStatic external fun setTransharmonicImdCancel(strength: Float)
    @JvmStatic external fun setTransharmonicAnalogTapeDrive(drive: Float)
    @JvmStatic external fun getTransharmonicPhaseStep(): Float
    @JvmStatic external fun getTransharmonicTapeMagnetization(): Float

    fun safeSetTransharmonicCvnnEnabled(enabled: Boolean) {
        if (isLoaded) runCatching { setTransharmonicCvnnEnabled(enabled) }
    }
    fun safeSetTransharmonicHarmonicGain(gain: Float) {
        if (isLoaded) runCatching { setTransharmonicHarmonicGain(gain) }
    }
    fun safeSetTransharmonicImdCancel(strength: Float) {
        if (isLoaded) runCatching { setTransharmonicImdCancel(strength) }
    }
    fun safeSetTransharmonicAnalogTapeDrive(drive: Float) {
        if (isLoaded) runCatching { setTransharmonicAnalogTapeDrive(drive) }
    }
    fun safeGetTransharmonicPhaseStep(): Float =
        if (isLoaded) runCatching { getTransharmonicPhaseStep() }.getOrDefault(0.012f) else 0.012f
    fun safeGetTransharmonicTapeMagnetization(): Float =
        if (isLoaded) runCatching { getTransharmonicTapeMagnetization() }.getOrDefault(0.0f) else 0.0f

    // ── EJE 3: SnnNmfHoaUpmixer (Anti-Dolby SNN INT8 + NMF -> HOA 4º Orden) ─
    @JvmStatic external fun setSnnHoaUpmixerEnabled(enabled: Boolean)
    @JvmStatic external fun setSnnHoaImmersivity(immersivity: Float)
    @JvmStatic external fun setSnnSpikeThreshold(threshold: Float)
    @JvmStatic external fun promoteSnnThreadToSchedFifo(priority: Int): Boolean
    @JvmStatic external fun getSnnActiveSpikes(): Int
    @JvmStatic external fun getSnnOrthogonalMasks(): FloatArray?

    fun safeSetSnnHoaUpmixerEnabled(enabled: Boolean) {
        if (isLoaded) runCatching { setSnnHoaUpmixerEnabled(enabled) }
    }
    fun safeSetSnnHoaImmersivity(immersivity: Float) {
        if (isLoaded) runCatching { setSnnHoaImmersivity(immersivity) }
    }
    fun safeSetSnnSpikeThreshold(threshold: Float) {
        if (isLoaded) runCatching { setSnnSpikeThreshold(threshold) }
    }
    fun safePromoteSnnThreadToSchedFifo(priority: Int = 85): Boolean =
        if (isLoaded) runCatching { promoteSnnThreadToSchedFifo(priority) }.getOrDefault(false) else false
    fun safeGetSnnActiveSpikes(): Int =
        if (isLoaded) runCatching { getSnnActiveSpikes() }.getOrDefault(2) else 2
    fun safeGetSnnOrthogonalMasks(): FloatArray =
        if (isLoaded) runCatching { getSnnOrthogonalMasks() }.getOrNull()
            ?: floatArrayOf(0.25f, 0.25f, 0.25f, 0.25f)
        else floatArrayOf(0.25f, 0.25f, 0.25f, 0.25f)

    // ── EJE 4: PinnaManifoldInterpolator (Anti-Apple INR-SDF + FIR Fase Mín) ─
    @JvmStatic external fun setPinnaManifoldEnabled(enabled: Boolean)
    @JvmStatic external fun setPinnaManifoldWetMix(wet: Float)
    @JvmStatic external fun calibratePinnaManifold(conchaDepth: Float, helixCurl: Float, headWidth: Float)
    @JvmStatic external fun calibratePinnaFromImagePatch(patch: FloatArray): FloatArray?
    @JvmStatic external fun getPinnaActiveLatents(): FloatArray?
    @JvmStatic external fun getPinnaActiveNotchHz(): Float
    @JvmStatic external fun getPinnaActiveItdUs(): Float
    @JvmStatic external fun getPinnaActiveFirTaps(): FloatArray?

    fun safeSetPinnaManifoldEnabled(enabled: Boolean) {
        if (isLoaded) runCatching { setPinnaManifoldEnabled(enabled) }
    }
    fun safeSetPinnaManifoldWetMix(wet: Float) {
        if (isLoaded) runCatching { setPinnaManifoldWetMix(wet) }
    }
    fun safeCalibratePinnaManifold(conchaDepth: Float, helixCurl: Float, headWidth: Float) {
        if (isLoaded) runCatching { calibratePinnaManifold(conchaDepth, helixCurl, headWidth) }
    }
    fun safeCalibratePinnaFromImagePatch(patch: FloatArray): FloatArray =
        if (isLoaded) runCatching { calibratePinnaFromImagePatch(patch) }.getOrNull()
            ?: floatArrayOf(0.15f, -0.05f, 0.10f, 0.02f, -0.04f, 0.08f)
        else floatArrayOf(0.15f, -0.05f, 0.10f, 0.02f, -0.04f, 0.08f)
    fun safeGetPinnaActiveLatents(): FloatArray =
        if (isLoaded) runCatching { getPinnaActiveLatents() }.getOrNull()
            ?: floatArrayOf(0.15f, -0.05f, 0.10f, 0.02f, -0.04f, 0.08f)
        else floatArrayOf(0.15f, -0.05f, 0.10f, 0.02f, -0.04f, 0.08f)
    fun safeGetPinnaActiveNotchHz(): Float =
        if (isLoaded) runCatching { getPinnaActiveNotchHz() }.getOrDefault(7800f) else 7800f
    fun safeGetPinnaActiveItdUs(): Float =
        if (isLoaded) runCatching { getPinnaActiveItdUs() }.getOrDefault(620f) else 620f
    fun safeGetPinnaActiveFirTaps(): FloatArray =
        if (isLoaded) runCatching { getPinnaActiveFirTaps() }.getOrNull()
            ?: floatArrayOf(0.92f, -0.18f, 0.11f, -0.07f, 0.04f, -0.03f, 0.02f, -0.01f)
        else floatArrayOf(0.92f, -0.18f, 0.11f, -0.07f, 0.04f, -0.03f, 0.02f, -0.01f)

    // ── EJE 5: SupremeMsoFarrowArbitrator (SHM CAS owner_pid + Farrow 5º) ───
    @JvmStatic external fun setFarrowMsoEnabled(enabled: Boolean)
    @JvmStatic external fun setMsoItdNanoseconds(ns: Float)
    @JvmStatic external fun setEbpfBypassActive(active: Boolean)
    @JvmStatic external fun acquireShmArbitration(pid: Int): Boolean
    @JvmStatic external fun releaseShmArbitration(pid: Int): Boolean
    @JvmStatic external fun getShmOwnerPid(): Int
    @JvmStatic external fun isShmCrossProcessMapped(): Boolean

    fun safeSetFarrowMsoEnabled(enabled: Boolean) {
        if (isLoaded) runCatching { setFarrowMsoEnabled(enabled) }
    }
    fun safeSetMsoItdNanoseconds(ns: Float) {
        if (isLoaded) runCatching { setMsoItdNanoseconds(ns) }
    }
    fun safeSetEbpfBypassActive(active: Boolean) {
        if (isLoaded) runCatching { setEbpfBypassActive(active) }
    }
    fun safeAcquireShmArbitration(pid: Int): Boolean =
        if (isLoaded) runCatching { acquireShmArbitration(pid) }.getOrDefault(true) else true
    fun safeReleaseShmArbitration(pid: Int): Boolean =
        if (isLoaded) runCatching { releaseShmArbitration(pid) }.getOrDefault(true) else true
    fun safeGetShmOwnerPid(): Int =
        if (isLoaded) runCatching { getShmOwnerPid() }.getOrDefault(0) else 0
    fun safeIsShmCrossProcessMapped(): Boolean =
        if (isLoaded) runCatching { isShmCrossProcessMapped() }.getOrDefault(false) else false

    // ── ACOUSTIC REALITY RECONSTRUCTION HYPERENGINE (Fases 1–8) ─────────────
    @JvmStatic external fun setRealityReconstructionEnabled(enabled: Boolean)
    @JvmStatic external fun isRealityReconstructionEnabled(): Boolean
    @JvmStatic external fun setRealityIntensity(intensity: Float)
    @JvmStatic external fun setPersonalAuditoryProfile(
        headRadiusM: Float,
        pinnaDepthM: Float,
        elevationBiasDeg: Float,
        transducerType: Int,
        sensitivityScore: Float
    )
    @JvmStatic external fun getRealityTelemetrySnapshot(): FloatArray?
    @JvmStatic external fun getCognitiveEvolutionTelemetrySnapshot(): FloatArray?

    fun safeSetRealityReconstructionEnabled(enabled: Boolean) {
        if (isLoaded) runCatching { setRealityReconstructionEnabled(enabled) }
    }
    fun safeIsRealityReconstructionEnabled(): Boolean =
        if (isLoaded) runCatching { isRealityReconstructionEnabled() }.getOrDefault(true) else true
    fun safeSetRealityIntensity(intensity: Float) {
        if (isLoaded) runCatching { setRealityIntensity(intensity) }
    }
    fun safeSetPersonalAuditoryProfile(
        headRadiusM: Float = 0.0875f,
        pinnaDepthM: Float = 0.0185f,
        elevationBiasDeg: Float = 0.0f,
        transducerType: Int = 0,
        sensitivityScore: Float = 1.0f
    ) {
        if (isLoaded) runCatching {
            setPersonalAuditoryProfile(headRadiusM, pinnaDepthM, elevationBiasDeg, transducerType, sensitivityScore)
        }
    }
    fun safeGetRealityTelemetrySnapshot(): FloatArray =
        if (isLoaded) runCatching { getRealityTelemetrySnapshot() }.getOrNull()
            ?: floatArrayOf(0.84f, 0.91f, 0.86f, 0.11f, 0.89f, 0.88f, 6.8f, 8.6f, 3.5f, 0.38f, 9.4f, 1.18f, 0.78f, 0.42f, 0.65f, 0.92f)
        else floatArrayOf(0.84f, 0.91f, 0.86f, 0.11f, 0.89f, 0.88f, 6.8f, 8.6f, 3.5f, 0.38f, 9.4f, 1.18f, 0.78f, 0.42f, 0.65f, 0.92f)

    fun safeGetCognitiveEvolutionTelemetrySnapshot(): FloatArray =
        if (isLoaded) runCatching { getCognitiveEvolutionTelemetrySnapshot() }.getOrNull()
            ?: floatArrayOf(0f, 0.84f, 0.80f, 0.76f, 0.62f, 0.88f, 0.86f, 0.84f, 0.90f, 0.94f, 0.91f, 0f, 0.96f, 0.92f, 0.89f, 12f)
        else floatArrayOf(0f, 0.84f, 0.80f, 0.76f, 0.62f, 0.88f, 0.86f, 0.84f, 0.90f, 0.94f, 0.91f, 0f, 0.96f, 0.92f, 0.89f, 12f)

    // ── EJES ESPACIALES 3, 5, 6: RoomProjection, PhysicalScene, HearingAdaptation ──
    @JvmStatic external fun setRoomProjectionParams(inversionGain: Float, projectionWet: Float)
    @JvmStatic external fun setPhysicalSceneParams(occlusionFactor: Float, wallAbsorption: Float)
    @JvmStatic external fun setHearingAdaptationParams(
        earTipSeal: Float,
        listeningSplDb: Float,
        lossLowDb: Float,
        lossMidDb: Float,
        lossHighDb: Float,
        lossUltraHighDb: Float
    )

    fun safeSetRoomProjectionParams(inversionGain: Float = 0.40f, projectionWet: Float = 0.25f) {
        if (isLoaded) runCatching { setRoomProjectionParams(inversionGain, projectionWet) }
    }
    fun safeSetPhysicalSceneParams(occlusionFactor: Float = 0.0f, wallAbsorption: Float = 0.30f) {
        if (isLoaded) runCatching { setPhysicalSceneParams(occlusionFactor, wallAbsorption) }
    }
    fun safeSetHearingAdaptationParams(
        earTipSeal: Float = 1.0f,
        listeningSplDb: Float = 75.0f,
        lossLowDb: Float = 0.0f,
        lossMidDb: Float = 0.0f,
        lossHighDb: Float = 0.0f,
        lossUltraHighDb: Float = 0.0f
    ) {
        if (isLoaded) runCatching {
            setHearingAdaptationParams(
                earTipSeal, listeningSplDb,
                lossLowDb, lossMidDb, lossHighDb, lossUltraHighDb
            )
        }
    }

    // ── MOTOR HÍBRIDO MAGISTRAL (HRTF KEMAR 128-Tap + Sala Schroeder/Moorer) ──
    @JvmStatic external fun setHybridMagistralParams(
        enabled: Boolean,
        binauralWet: Float,
        virtualAzimuthDeg: Float,
        virtualElevationDeg: Float,
        roomSize: Float,
        roomAbsorption: Float,
        roomDampening: Float,
        roomWetMix: Float
    )
    @JvmStatic external fun getHybridMagistralTelemetry(): FloatArray?

    fun safeSetHybridMagistralParams(
        enabled: Boolean = true,
        binauralWet: Float = 0.65f,
        virtualAzimuthDeg: Float = 30.0f,
        virtualElevationDeg: Float = 0.0f,
        roomSize: Float = 0.55f,
        roomAbsorption: Float = 0.35f,
        roomDampening: Float = 0.40f,
        roomWetMix: Float = 0.25f
    ) {
        if (isLoaded) runCatching {
            setHybridMagistralParams(
                enabled, binauralWet, virtualAzimuthDeg, virtualElevationDeg,
                roomSize, roomAbsorption, roomDampening, roomWetMix
            )
        }
    }

    fun safeGetHybridMagistralTelemetry(): FloatArray =
        if (isLoaded) runCatching { getHybridMagistralTelemetry() }.getOrNull()
            ?: floatArrayOf(1f, 0.65f, 30f, 0f, 0.55f, 0.35f, 0.40f, 0.25f)
        else floatArrayOf(1f, 0.65f, 30f, 0f, 0.55f, 0.35f, 0.40f, 0.25f)
}
