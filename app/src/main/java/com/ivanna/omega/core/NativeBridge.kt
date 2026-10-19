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
            if (com.ivanna.omega.spatial.IvannaSpatialNative.isLoaded) {
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
            if (com.ivanna.omega.spatial.IvannaSpatialNative.isLoaded) {
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
            val spActive = if (com.ivanna.omega.spatial.IvannaSpatialNative.isLoaded) {
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
        if (isLoaded) runCatching { getWarpedLatticeDeclippedPeaks() }.getOrDefault(-1) else -1
    fun safeGetWarpedLatticeSubSampleDelay(): Float =
        if (isLoaded) runCatching { getWarpedLatticeSubSampleDelay() }.getOrDefault(Float.NaN) else Float.NaN
    fun safeRunWarpedLatticeLoopbackCalibration(f0Hz: Float = 92.0f, mu: Float = 0.005f) {
        if (isLoaded) runCatching { runWarpedLatticeLoopbackCalibration(f0Hz, mu) }
    }
    fun safeGetWarpedLatticeKappas(): FloatArray =
        if (isLoaded) runCatching { getWarpedLatticeKappas() }.getOrNull()
            ?: floatArrayOf()
        else floatArrayOf()

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
        if (isLoaded) runCatching { getTransharmonicPhaseStep() }.getOrDefault(Float.NaN) else Float.NaN
    fun safeGetTransharmonicTapeMagnetization(): Float =
        if (isLoaded) runCatching { getTransharmonicTapeMagnetization() }.getOrDefault(Float.NaN) else Float.NaN

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
        if (isLoaded) runCatching { getSnnActiveSpikes() }.getOrDefault(-1) else -1
    fun safeGetSnnOrthogonalMasks(): FloatArray =
        if (isLoaded) runCatching { getSnnOrthogonalMasks() }.getOrNull()
            ?: floatArrayOf()
        else floatArrayOf()

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
            ?: floatArrayOf()
        else floatArrayOf()
    fun safeGetPinnaActiveLatents(): FloatArray =
        if (isLoaded) runCatching { getPinnaActiveLatents() }.getOrNull()
            ?: floatArrayOf()
        else floatArrayOf()
    fun safeGetPinnaActiveNotchHz(): Float =
        if (isLoaded) runCatching { getPinnaActiveNotchHz() }.getOrDefault(Float.NaN) else Float.NaN
    fun safeGetPinnaActiveItdUs(): Float =
        if (isLoaded) runCatching { getPinnaActiveItdUs() }.getOrDefault(Float.NaN) else Float.NaN
    fun safeGetPinnaActiveFirTaps(): FloatArray =
        if (isLoaded) runCatching { getPinnaActiveFirTaps() }.getOrNull()
            ?: floatArrayOf()
        else floatArrayOf()

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
        if (isLoaded) runCatching { acquireShmArbitration(pid) }.getOrDefault(false) else false
    fun safeReleaseShmArbitration(pid: Int): Boolean =
        if (isLoaded) runCatching { releaseShmArbitration(pid) }.getOrDefault(false) else false
    fun safeGetShmOwnerPid(): Int =
        if (isLoaded) runCatching { getShmOwnerPid() }.getOrDefault(-1) else -1
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
        if (isLoaded) runCatching { isRealityReconstructionEnabled() }.getOrDefault(false) else false
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
    // Honestos: array vacío = motor nativo sin snapshot vivo (antes devolvían
    // valores inventados 0.84/0.91/… que la UI mostraba como si fueran medidos).
    fun safeGetRealityTelemetrySnapshot(): FloatArray =
        if (isLoaded) runCatching { getRealityTelemetrySnapshot() }.getOrNull() ?: FloatArray(0)
        else FloatArray(0)

    fun safeGetCognitiveEvolutionTelemetrySnapshot(): FloatArray =
        if (isLoaded) runCatching { getCognitiveEvolutionTelemetrySnapshot() }.getOrNull() ?: FloatArray(0)
        else FloatArray(0)

    // Variantes null-safe: null = sin telemetría viva.
    fun safeGetRealityTelemetryOrNull(): FloatArray? =
        safeGetRealityTelemetrySnapshot().takeIf { it.isNotEmpty() }

    fun safeGetCognitiveTelemetryOrNull(): FloatArray? =
        safeGetCognitiveEvolutionTelemetrySnapshot().takeIf { it.isNotEmpty() }

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
        if (isLoaded) runCatching { getHybridMagistralTelemetry() }.getOrNull() ?: FloatArray(0)
        else FloatArray(0)
}
