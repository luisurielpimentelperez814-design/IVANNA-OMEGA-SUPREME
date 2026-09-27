package com.ivanna.omega.ui

import android.content.Context
import android.os.Process
import com.ivanna.omega.core.NativeBridge
import com.ivanna.omega.magisk.OmegaEngineBridge
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

/**
 * Estado persistente de los 5 Ejes de Supremacía Cuántico-Neuromórfica (Prompt Maestro 2026).
 * Cada campo está cableado de extremo a extremo:
 *   Compose UI <-> SharedPreferences <-> PersistedStateRestorer <-> NativeBridge (JNI) <-> C++23 DSP
 */
data class SupremeAxesState(
    // EJE 1: WarpedLatticeTransducerInverter (Anti-Dirac + Master De-Clipper)
    val warpedLatticeEnabled: Boolean = true,
    val warpedLatticeMicroChirp: Boolean = true,
    val warpedLatticeBlDrive: Float = 0.50f,
    val warpedLatticeLambda: Float = 0.756f,

    // EJE 2: PhaseCoherentTransharmonicSynthesizer (Anti-DSEE CVNN+DDSP + Cinta 2" Jiles-Atherton)
    val transharmonicCvnnEnabled: Boolean = true,
    val transharmonicHarmonicGain: Float = 0.28f,
    val transharmonicImdCancel: Float = 0.85f,
    val transharmonicAnalogTapeDrive: Float = 0.25f,

    // EJE 3: SnnNmfHoaUpmixer (Anti-Dolby SNN INT8 + NMF -> HOA 4º Orden)
    val snnHoaUpmixerEnabled: Boolean = true,
    val snnHoaImmersivity: Float = 0.65f,
    val snnSpikeThreshold: Float = 0.55f,
    val snnSchedFifoPromoted: Boolean = false,

    // EJE 4: PinnaManifoldInterpolator (Anti-Apple INR-SDF + FIR Fase Mínima)
    val pinnaManifoldEnabled: Boolean = true,
    val pinnaManifoldWetMix: Float = 0.65f,
    val pinnaConchaDepth: Float = 0.15f,
    val pinnaHelixCurl: Float = -0.10f,
    val pinnaHeadWidth: Float = 0.08f,

    // EJE 5: SupremeMsoFarrowArbitrator (eBPF/SHM CAS owner_pid + Farrow 5º Orden)
    val farrowMsoEnabled: Boolean = true,
    val msoItdNanoseconds: Float = 0.0f,
    val ebpfBypassActive: Boolean = false,
    val shmArbitrationLockedByApp: Boolean = false,

    // ACOUSTIC REALITY RECONSTRUCTION HYPERENGINE (Fases 1–8)
    val realityReconstructionEnabled: Boolean = true,
    val realityIntensity: Float = 0.85f,
    val personalHeadRadiusM: Float = 0.0875f,
    val personalPinnaDepthM: Float = 0.0185f,
    val personalElevationBiasDeg: Float = 0.0f,
    val personalTransducerType: Int = 0
)

object SupremeAxesPrefs {
    private const val PREFS_NAME = "ivanna_supreme_five_axes_prefs"
    private val ioScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    private val _stateFlow = MutableStateFlow(SupremeAxesState())
    val stateFlow: StateFlow<SupremeAxesState> = _stateFlow.asStateFlow()

    fun load(context: Context): SupremeAxesState {
        val p = context.applicationContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        val d = SupremeAxesState()

        fun bool(k: String, def: Boolean): Boolean =
            runCatching { p.getBoolean(k, def) }.getOrDefault(def)
        fun flt(k: String, def: Float): Float =
            runCatching { p.getFloat(k, def) }.getOrDefault(def)

        return SupremeAxesState(
            warpedLatticeEnabled      = bool("warpedLatticeEnabled", d.warpedLatticeEnabled),
            warpedLatticeMicroChirp   = bool("warpedLatticeMicroChirp", d.warpedLatticeMicroChirp),
            warpedLatticeBlDrive      = flt("warpedLatticeBlDrive", d.warpedLatticeBlDrive).coerceIn(0f, 1f),
            warpedLatticeLambda       = flt("warpedLatticeLambda", d.warpedLatticeLambda).coerceIn(0.50f, 0.85f),

            transharmonicCvnnEnabled  = bool("transharmonicCvnnEnabled", d.transharmonicCvnnEnabled),
            transharmonicHarmonicGain = flt("transharmonicHarmonicGain", d.transharmonicHarmonicGain).coerceIn(0f, 1f),
            transharmonicImdCancel    = flt("transharmonicImdCancel", d.transharmonicImdCancel).coerceIn(0f, 1f),
            transharmonicAnalogTapeDrive = flt("transharmonicAnalogTapeDrive", d.transharmonicAnalogTapeDrive).coerceIn(0f, 1f),

            snnHoaUpmixerEnabled      = bool("snnHoaUpmixerEnabled", d.snnHoaUpmixerEnabled),
            snnHoaImmersivity         = flt("snnHoaImmersivity", d.snnHoaImmersivity).coerceIn(0f, 1f),
            snnSpikeThreshold         = flt("snnSpikeThreshold", d.snnSpikeThreshold).coerceIn(0.15f, 1.50f),
            snnSchedFifoPromoted      = bool("snnSchedFifoPromoted", d.snnSchedFifoPromoted),

            pinnaManifoldEnabled      = bool("pinnaManifoldEnabled", d.pinnaManifoldEnabled),
            pinnaManifoldWetMix       = flt("pinnaManifoldWetMix", d.pinnaManifoldWetMix).coerceIn(0f, 1f),
            pinnaConchaDepth          = flt("pinnaConchaDepth", d.pinnaConchaDepth).coerceIn(-1f, 1f),
            pinnaHelixCurl            = flt("pinnaHelixCurl", d.pinnaHelixCurl).coerceIn(-1f, 1f),
            pinnaHeadWidth            = flt("pinnaHeadWidth", d.pinnaHeadWidth).coerceIn(-1f, 1f),

            farrowMsoEnabled          = bool("farrowMsoEnabled", d.farrowMsoEnabled),
            msoItdNanoseconds         = flt("msoItdNanoseconds", d.msoItdNanoseconds).coerceIn(-50000f, 50000f),
            ebpfBypassActive          = bool("ebpfBypassActive", d.ebpfBypassActive),
            shmArbitrationLockedByApp = bool("shmArbitrationLockedByApp", d.shmArbitrationLockedByApp),
            realityReconstructionEnabled = bool("realityReconstructionEnabled", d.realityReconstructionEnabled),
            realityIntensity          = flt("realityIntensity", d.realityIntensity).coerceIn(0f, 1f),
            personalHeadRadiusM       = flt("personalHeadRadiusM", d.personalHeadRadiusM).coerceIn(0.070f, 0.110f),
            personalPinnaDepthM       = flt("personalPinnaDepthM", d.personalPinnaDepthM).coerceIn(0.010f, 0.030f),
            personalElevationBiasDeg  = flt("personalElevationBiasDeg", d.personalElevationBiasDeg).coerceIn(-25f, 25f),
            personalTransducerType    = runCatching { p.getInt("personalTransducerType", d.personalTransducerType) }.getOrDefault(d.personalTransducerType).coerceIn(0, 3)
        ).also { _stateFlow.value = it }
    }

    fun save(context: Context, s: SupremeAxesState) {
        _stateFlow.value = s
        context.applicationContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE).edit()
            .putBoolean("warpedLatticeEnabled", s.warpedLatticeEnabled)
            .putBoolean("warpedLatticeMicroChirp", s.warpedLatticeMicroChirp)
            .putFloat("warpedLatticeBlDrive", s.warpedLatticeBlDrive)
            .putFloat("warpedLatticeLambda", s.warpedLatticeLambda)
            .putBoolean("transharmonicCvnnEnabled", s.transharmonicCvnnEnabled)
            .putFloat("transharmonicHarmonicGain", s.transharmonicHarmonicGain)
            .putFloat("transharmonicImdCancel", s.transharmonicImdCancel)
            .putFloat("transharmonicAnalogTapeDrive", s.transharmonicAnalogTapeDrive)
            .putBoolean("snnHoaUpmixerEnabled", s.snnHoaUpmixerEnabled)
            .putFloat("snnHoaImmersivity", s.snnHoaImmersivity)
            .putFloat("snnSpikeThreshold", s.snnSpikeThreshold)
            .putBoolean("snnSchedFifoPromoted", s.snnSchedFifoPromoted)
            .putBoolean("pinnaManifoldEnabled", s.pinnaManifoldEnabled)
            .putFloat("pinnaManifoldWetMix", s.pinnaManifoldWetMix)
            .putFloat("pinnaConchaDepth", s.pinnaConchaDepth)
            .putFloat("pinnaHelixCurl", s.pinnaHelixCurl)
            .putFloat("pinnaHeadWidth", s.pinnaHeadWidth)
            .putBoolean("farrowMsoEnabled", s.farrowMsoEnabled)
            .putFloat("msoItdNanoseconds", s.msoItdNanoseconds)
            .putBoolean("ebpfBypassActive", s.ebpfBypassActive)
            .putBoolean("shmArbitrationLockedByApp", s.shmArbitrationLockedByApp)
            .putBoolean("realityReconstructionEnabled", s.realityReconstructionEnabled)
            .putFloat("realityIntensity", s.realityIntensity)
            .putFloat("personalHeadRadiusM", s.personalHeadRadiusM)
            .putFloat("personalPinnaDepthM", s.personalPinnaDepthM)
            .putFloat("personalElevationBiasDeg", s.personalElevationBiasDeg)
            .putInt("personalTransducerType", s.personalTransducerType)
            .apply()
        pushToDaemonRutaB(s)
    }

    /**
     * Sincroniza asíncronamente los 5 Ejes con `ivanna_daemon` -> `OmegaControlBus` -> `omega_effect.so` (Ruta B).
     */
    fun pushToDaemonRutaB(s: SupremeAxesState) {
        ioScope.launch {
            runCatching {
                OmegaEngineBridge.pushSupremeAxesState(
                    warpedLatticeEnabled      = s.warpedLatticeEnabled,
                    warpedLatticeMicroChirp   = s.warpedLatticeMicroChirp,
                    warpedLatticeBlDrive      = s.warpedLatticeBlDrive,
                    warpedLatticeLambda       = s.warpedLatticeLambda,
                    transharmonicCvnnEnabled  = s.transharmonicCvnnEnabled,
                    transharmonicHarmonicGain = s.transharmonicHarmonicGain,
                    transharmonicImdCancel    = s.transharmonicImdCancel,
                    transharmonicAnalogTapeDrive = s.transharmonicAnalogTapeDrive,
                    snnHoaUpmixerEnabled      = s.snnHoaUpmixerEnabled,
                    snnHoaImmersivity         = s.snnHoaImmersivity,
                    snnSpikeThreshold         = s.snnSpikeThreshold,
                    pinnaManifoldEnabled      = s.pinnaManifoldEnabled,
                    pinnaManifoldWetMix       = s.pinnaManifoldWetMix,
                    pinnaConchaDepth          = s.pinnaConchaDepth,
                    pinnaHelixCurl            = s.pinnaHelixCurl,
                    pinnaHeadWidth            = s.pinnaHeadWidth,
                    farrowMsoEnabled          = s.farrowMsoEnabled,
                    msoItdNanoseconds         = s.msoItdNanoseconds,
                    ebpfBypassActive          = s.ebpfBypassActive
                )
            }
        }
    }

    /**
     * Empuja el estado completo de los 5 Ejes al pipeline C++23 vía NativeBridge (Ruta A)
     * y al bus SHM inter-proceso vía OmegaEngineBridge (Ruta B).
     * Operación 100% lock-free y noexcept.
     */
    fun applyToNative(s: SupremeAxesState) {
        // EJE 1: Warped Lattice Transducer Inverter
        NativeBridge.safeSetWarpedLatticeLambda(s.warpedLatticeLambda)
        NativeBridge.safeSetWarpedLatticeBlDrive(s.warpedLatticeBlDrive)
        NativeBridge.safeSetWarpedLatticeMicroChirp(s.warpedLatticeMicroChirp)
        NativeBridge.safeSetWarpedLatticeEnabled(s.warpedLatticeEnabled)

        // EJE 2: Phase-Coherent Transharmonic Synthesizer (CVNN + DDSP + Cinta 2" Jiles-Atherton)
        NativeBridge.safeSetTransharmonicHarmonicGain(s.transharmonicHarmonicGain)
        NativeBridge.safeSetTransharmonicImdCancel(s.transharmonicImdCancel)
        NativeBridge.safeSetTransharmonicAnalogTapeDrive(s.transharmonicAnalogTapeDrive)
        NativeBridge.safeSetTransharmonicCvnnEnabled(s.transharmonicCvnnEnabled)

        // EJE 3: SNN INT8 + Online NMF -> 4th Order HOA Upmixer
        NativeBridge.safeSetSnnHoaImmersivity(s.snnHoaImmersivity)
        NativeBridge.safeSetSnnSpikeThreshold(s.snnSpikeThreshold)
        NativeBridge.safeSetSnnHoaUpmixerEnabled(s.snnHoaUpmixerEnabled)
        if (s.snnSchedFifoPromoted) {
            NativeBridge.safePromoteSnnThreadToSchedFifo(85)
        }

        // EJE 4: Pinna Manifold INR-SDF + 32-Tap Minimum-Phase FIR
        NativeBridge.safeCalibratePinnaManifold(s.pinnaConchaDepth, s.pinnaHelixCurl, s.pinnaHeadWidth)
        NativeBridge.safeSetPinnaManifoldWetMix(s.pinnaManifoldWetMix)
        NativeBridge.safeSetPinnaManifoldEnabled(s.pinnaManifoldEnabled)

        // EJE 5: SHM Lock-Free CAS Arbitrator + 5th-Order Farrow MSO Delay
        NativeBridge.safeSetMsoItdNanoseconds(s.msoItdNanoseconds)
        NativeBridge.safeSetEbpfBypassActive(s.ebpfBypassActive)
        NativeBridge.safeSetFarrowMsoEnabled(s.farrowMsoEnabled)
        val pid = runCatching { Process.myPid() }.getOrDefault(1001)
        if (s.shmArbitrationLockedByApp) {
            NativeBridge.safeAcquireShmArbitration(pid)
        }

        // ACOUSTIC REALITY RECONSTRUCTION HYPERENGINE (Fases 1–8)
        NativeBridge.safeSetRealityIntensity(s.realityIntensity)
        NativeBridge.safeSetPersonalAuditoryProfile(
            headRadiusM      = s.personalHeadRadiusM,
            pinnaDepthM      = s.personalPinnaDepthM,
            elevationBiasDeg = s.personalElevationBiasDeg,
            transducerType   = s.personalTransducerType,
            sensitivityScore = 1.0f
        )
        NativeBridge.safeSetRealityReconstructionEnabled(s.realityReconstructionEnabled)

        // Sincronizar también Ruta B (Daemon + AudioFlinger HAL Effect)
        pushToDaemonRutaB(s)
        ioScope.launch {
            runCatching {
                com.ivanna.omega.magisk.MagiskBridge.sendCommand(
                    "{\"action\":\"SET_REALITY_RECONSTRUCTION\",\"enabled\":${if (s.realityReconstructionEnabled) "true" else "false"},\"intensity\":${s.realityIntensity}}"
                )
            }
        }
    }
}
