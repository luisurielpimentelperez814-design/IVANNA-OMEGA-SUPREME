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
        } catch (t: Throwable) {
            Log.e(TAG, "Failed to call setCochlearIntensity", t)
        }
    }

    fun safeIsCochlearActive(): Boolean {
        if (!isLoaded) return false
        return try {
            isCochlearActive()
        } catch (t: Throwable) {
            false
        }
    }

    fun safeGetCochlearIntensity(): Float {
        if (!isLoaded) return 0.35f
        return try {
            getCochlearIntensity()
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
    fun safeGetWarpedLatticeSubSampleDelay(): Float =
        if (isLoaded) runCatching { getWarpedLatticeSubSampleDelay() }.getOrDefault(0.24f) else 0.24f

    // ── EJE 2: PhaseCoherentTransharmonicSynthesizer (Anti-DSEE CVNN+DDSP) ──
    @JvmStatic external fun setTransharmonicCvnnEnabled(enabled: Boolean)
    @JvmStatic external fun setTransharmonicHarmonicGain(gain: Float)
    @JvmStatic external fun setTransharmonicImdCancel(strength: Float)
    @JvmStatic external fun getTransharmonicPhaseStep(): Float

    fun safeSetTransharmonicCvnnEnabled(enabled: Boolean) {
        if (isLoaded) runCatching { setTransharmonicCvnnEnabled(enabled) }
    }
    fun safeSetTransharmonicHarmonicGain(gain: Float) {
        if (isLoaded) runCatching { setTransharmonicHarmonicGain(gain) }
    }
    fun safeSetTransharmonicImdCancel(strength: Float) {
        if (isLoaded) runCatching { setTransharmonicImdCancel(strength) }
    }
    fun safeGetTransharmonicPhaseStep(): Float =
        if (isLoaded) runCatching { getTransharmonicPhaseStep() }.getOrDefault(0.012f) else 0.012f

    // ── EJE 3: SnnNmfHoaUpmixer (Anti-Dolby SNN INT8 + NMF -> HOA 4º Orden) ─
    @JvmStatic external fun setSnnHoaUpmixerEnabled(enabled: Boolean)
    @JvmStatic external fun setSnnHoaImmersivity(immersivity: Float)
    @JvmStatic external fun setSnnSpikeThreshold(threshold: Float)
    @JvmStatic external fun promoteSnnThreadToSchedFifo(priority: Int): Boolean
    @JvmStatic external fun getSnnActiveSpikes(): Int

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

    // ── EJE 4: PinnaManifoldInterpolator (Anti-Apple INR-SDF + FIR Fase Mín) ─
    @JvmStatic external fun setPinnaManifoldEnabled(enabled: Boolean)
    @JvmStatic external fun setPinnaManifoldWetMix(wet: Float)
    @JvmStatic external fun calibratePinnaManifold(conchaDepth: Float, helixCurl: Float, headWidth: Float)
    @JvmStatic external fun getPinnaActiveNotchHz(): Float
    @JvmStatic external fun getPinnaActiveItdUs(): Float

    fun safeSetPinnaManifoldEnabled(enabled: Boolean) {
        if (isLoaded) runCatching { setPinnaManifoldEnabled(enabled) }
    }
    fun safeSetPinnaManifoldWetMix(wet: Float) {
        if (isLoaded) runCatching { setPinnaManifoldWetMix(wet) }
    }
    fun safeCalibratePinnaManifold(conchaDepth: Float, helixCurl: Float, headWidth: Float) {
        if (isLoaded) runCatching { calibratePinnaManifold(conchaDepth, helixCurl, headWidth) }
    }
    fun safeGetPinnaActiveNotchHz(): Float =
        if (isLoaded) runCatching { getPinnaActiveNotchHz() }.getOrDefault(7800f) else 7800f
    fun safeGetPinnaActiveItdUs(): Float =
        if (isLoaded) runCatching { getPinnaActiveItdUs() }.getOrDefault(620f) else 620f

    // ── EJE 5: SupremeMsoFarrowArbitrator (SHM CAS owner_pid + Farrow 5º) ───
    @JvmStatic external fun setFarrowMsoEnabled(enabled: Boolean)
    @JvmStatic external fun setMsoItdNanoseconds(ns: Float)
    @JvmStatic external fun setEbpfBypassActive(active: Boolean)
    @JvmStatic external fun acquireShmArbitration(pid: Int): Boolean
    @JvmStatic external fun releaseShmArbitration(pid: Int): Boolean
    @JvmStatic external fun getShmOwnerPid(): Int

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
}
