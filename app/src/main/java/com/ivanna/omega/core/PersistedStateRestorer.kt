package com.ivanna.omega.core

import android.content.Context
import android.util.Log
import com.ivanna.omega.magisk.OmegaEngineBridge
import com.ivanna.omega.spatial.IvannaSpatialEngine
import com.ivanna.omega.spatial.IvannaSpatialManager
import com.ivanna.omega.ui.AdaptiveControlsPrefs
import com.ivanna.omega.ui.SpatialAudioPrefs

object PersistedStateRestorer {
    private const val TAG = "PersistedStateRestorer"

    suspend fun restore(context: Context) {
        runCatching { com.ivanna.omega.audio.HiResAudioManager.restore(context) }
        val ctx = context.applicationContext
        val saPrefs = SpatialAudioPrefs.load(ctx)
        val route = com.ivanna.omega.audio.AudioRouteManager.detectOutputRoute()
        val externalStereo = com.ivanna.omega.audio.ExternalStereoPolicy.usesExternalSpeakers(
            route, saPrefs.externalStereoLineOut)
        val hrtfWet = com.ivanna.omega.audio.ExternalStereoPolicy.hrtfWetForRoute(
            route, saPrefs.externalStereoLineOut, saPrefs.hrtfEnabled)
        val hrtfAllowed = hrtfWet > 0f
        val safAllowed = saPrefs.safEnabled &&
            com.ivanna.omega.audio.ExternalStereoPolicy.allowHeadphoneHrtf(
                route, saPrefs.externalStereoLineOut, saPrefs.hrtfEnabled)

        // 1. Restaurar AdaptiveControlsPrefs
        val acPrefs = AdaptiveControlsPrefs.load(ctx)
        // El motor híbrido es estado global nativo compartido por varias pantallas.
        // Restaurarlo aquí evita que dependa de abrir SoundScreen/SpatialAudioPanel.
        NativeBridge.safeSetHybridMagistralParams(
            enabled = acPrefs.hybridMagistralEnabled,
            binauralWet = if (hrtfAllowed) acPrefs.hybridBinauralWet * hrtfWet else 0f,
            virtualAzimuthDeg = acPrefs.binauralAzimuth.takeIf { kotlin.math.abs(it) > 1f } ?: 30f,
            virtualElevationDeg = acPrefs.binauralElevation,
            roomSize = acPrefs.hybridRoomSize,
            roomAbsorption = acPrefs.hybridRoomAbsorption,
            roomDampening = acPrefs.hybridRoomDampening,
            roomWetMix = if (externalStereo) 0f else acPrefs.hybridRoomWetMix
        )
        if (IvannaNativeLib.isLoaded) {
            runCatching { com.ivanna.omega.audio.AudioEngine.nativeSetHrtfWetDryStatic(hrtfWet) }
            runCatching { IvannaNativeLib.nativeSetEta(acPrefs.nhoEta) }
            runCatching { IvannaNativeLib.nativeSetHarmonicGain(acPrefs.nhoHarmonicGain) }
            runCatching { IvannaNativeLib.nativeSetBeta(acPrefs.nhoLateralInhib) }
            runCatching { IvannaNativeLib.nativeSetAlpha(acPrefs.nhoOhcGain) }
            
            // EQ evolutivo 512 (Ruta A): el nativo arranca en ON; se reaplica la eleccion guardada.
            runCatching { EvolutionaryEqPrefs.restore(ctx) }

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
                com.ivanna.omega.neuromorphic.PiLstmBridge.setAgc(acPrefs.agcTarget, acPrefs.agcRate)
            }
            
            // Azimuth/Elevation
            val aziRad = acPrefs.binauralAzimuth * Math.PI.toFloat() / 180f
            val eleRad = acPrefs.binauralElevation * Math.PI.toFloat() / 180f
            IvannaSpatialEngine.setAzimuth(aziRad)
            IvannaSpatialEngine.setElevation(eleRad)
            runCatching { IvannaNativeLib.nativeSetSpatialAngleRad(aziRad) }
            
            // Intelligent Upmixing (HOA)
            val paramStore = com.ivanna.omega.core.ParameterStore(ctx)
            runCatching { IvannaNativeLib.nativeSetIntelligentUpmixingEnabled(paramStore.isHoaUpmixingEnabled() && hrtfAllowed) }
            runCatching { IvannaNativeLib.nativeSetUpmixingImmersivity(paramStore.getHoaImmersivity()) }
            runCatching { com.ivanna.omega.magisk.MagiskBridge.sendCommand("{\"action\":\"SET_PERCEPTUAL_STATE\",\"upmixingEnabled\":${if (paramStore.isHoaUpmixingEnabled() && hrtfAllowed) 1.0f else 0.0f},\"upmixingImmersivity\":${paramStore.getHoaImmersivity()}}") }

            // Wave Field Synthesis (WFS) & SPSC Ring Buffer
            runCatching { IvannaNativeLib.nativeSetWfsEnabled(paramStore.isWfsEnabled() && !externalStereo) }
            runCatching { IvannaNativeLib.nativeSetWfsSpread(paramStore.getWfsSpread()) }
            // Modo AUX/altavoces: restaura estado, crossover y coherencia (ruta A + daemon/ruta B).
            runCatching { IvannaNativeLib.nativeSetAuxField(paramStore.isAuxFieldEnabled(), paramStore.getAuxXoverHz(), paramStore.getAuxCoherence()) }
            // NPE neuromórfico: estado al módulo (Ruta B). Sin esto el módulo arrancaba con su valor por defecto.
            // PRECEDENCIA (v2.5.411, tarea 3b): el motor NPE VIVO se alimenta
            // de AdaptiveControlsPrefs (ivanna_adaptive_controls), NO de
            // ParameterStore: los sliders de la UI solo escriben ahí y los
            // valores NPE de ParameterStore (ivanna_omega_params) difieren
            // de estos en el teléfono de Esse. ParameterStore NPE solo
            // alimenta el sync al cDSP (OemViewModel). No se re-apunta nada:
            // cambiar la fuente cambiaría el sonido sin evidencia ni ABX.
            runCatching {
                com.ivanna.omega.neuromorphic.PiLstmBridge.restoreModuleNpe(paramStore.isNpeBypass())
                // Los valores que la UI MUESTRA (guardados o por defecto) son los que debe usar el motor desde el arranque;
                // antes solo se aplicaban al mover un slider, y el motor corría con otros valores internos.
                val npe = com.ivanna.omega.ui.AdaptiveControlsPrefs.load(context)
                val pi = com.ivanna.omega.neuromorphic.PiLstmBridge
                pi.setHarmonicGain(npe.npeHarmonic)
                pi.setBeta(npe.npeLateralInhib)
                pi.setAlpha(npe.npeOhcCompression)
                pi.setMasterGain(npe.npeMasterGain)
                if (paramStore.isNpeBypass()) pi.setBypass(true)   // bypass guardado: aplicado desde el arranque, sin abrir la pestaña
            }
            // Corrección acústica de sala medida: si estaba activa, la restaura (ruta A + daemon/ruta B); si no, ceros.
            runCatching {
                val roomG = paramStore.getRoomCorrectionGains()
                IvannaNativeLib.nativeSetRoomCorrection(if (paramStore.isRoomCorrectionEnabled() && roomG != null) roomG else FloatArray(10))
            }
            // Impacto predictivo (PunchEngine): restaura la cantidad (ruta A + daemon/ruta B).
            runCatching { IvannaNativeLib.nativeSetPunch(paramStore.getPunchAmount()) }
            // Crossfeed de graves con ITD (Ruta A): restaura la intensidad (0 = apagado).
            runCatching { IvannaNativeLib.nativeSetCrossfeed(paramStore.getCrossfeedStrength()) }
            // ORBE — Binaural Esférico de Campo Cercano (v2.5.363): restaura los 6
            // parámetros (amount 0 = apagado por defecto) en la Ruta A por JNI y en
            // la Ruta B por SET_PERCEPTUAL_STATE al daemon (snapshot ABI v9).
            // v2.5.386 — MIGRACIÓN DEL SOBRE 380: antes de reenviar nada al
            // daemon, clampa los valores persistidos por la 384 (HOLOS 0.75 /
            // width 1.25 / diffuse 0.65, ORBE spread 1.2) al sobre 380.
            runCatching { paramStore.migrateTo386Envelope() }
            runCatching {
                IvannaNativeLib.nativeSetOrbeParams(
                    paramStore.getOrbeAmount(), paramStore.getOrbeDistanceM(),
                    paramStore.getOrbeSpread(), paramStore.getOrbeProximityDb(),
                    paramStore.getOrbeDiffuse(), paramStore.isOrbeHeadTrackingEnabled()
                )
            }
            // Ψ (v2.5.388): radio de cabeza personalizado medido en
            // HeadMeasureScreen (KEMAR 0.0875 si nunca se midió).
            runCatching {
                IvannaNativeLib.nativeSetOrbeHeadRadiusM(paramStore.getOrbeHeadRadiusM())
            }
            runCatching {
                com.ivanna.omega.magisk.MagiskBridge.sendCommand(
                    """{"action":"SET_PERCEPTUAL_STATE","orbeAmount":${paramStore.getOrbeAmount()},"orbeDistanceM":${paramStore.getOrbeDistanceM()},"orbeSpread":${paramStore.getOrbeSpread()},"orbeProximityDb":${paramStore.getOrbeProximityDb()},"orbeDiffuse":${paramStore.getOrbeDiffuse()},"orbeHeadTracking":${if (paramStore.isOrbeHeadTrackingEnabled()) 1.0f else 0.0f}}"""
                )
            }
            // HOLOS — Operador Único de Campo Acústico (v2.5.364): restaura los 13
            // parámetros (amount 0 = apagado por defecto) en la Ruta A por JNI y en
            // la Ruta B por SET_PERCEPTUAL_STATE al daemon (snapshot ABI v10).
            // El EQ estático de Γ se restaura de ParameterStore (plano = no-op).
            runCatching {
                IvannaNativeLib.nativeSetHolosParams(
                    paramStore.getHolosAmount(), paramStore.getHolosDistanceM(),
                    paramStore.getHolosSpread(), paramStore.getHolosProximityDb(),
                    paramStore.getHolosDiffuse(), paramStore.getHolosHarmonics(),
                    paramStore.getHolosWarmth(), paramStore.getHolosDynamics(),
                    paramStore.getHolosWidth(), paramStore.getHolosGamma(),
                    paramStore.getHolosRoomRt60(), paramStore.isHolosHeadTrackingEnabled(),
                    paramStore.getHolosBandCount()
                )
                IvannaNativeLib.nativeSetHolosEq(
                    paramStore.getHolosEqLow(), paramStore.getHolosEqMid(),
                    paramStore.getHolosEqHigh(), paramStore.getHolosEqPresence(),
                    paramStore.getHolosEqTiltDb(), paramStore.getHolosEqFreq()
                )
            }
            runCatching {
                com.ivanna.omega.magisk.MagiskBridge.sendCommand(
                    """{"action":"SET_PERCEPTUAL_STATE","holosAmount":${paramStore.getHolosAmount()},"holosDistanceM":${paramStore.getHolosDistanceM()},"holosSpread":${paramStore.getHolosSpread()},"holosProximityDb":${paramStore.getHolosProximityDb()},"holosDiffuse":${paramStore.getHolosDiffuse()},"holosHarmonics":${paramStore.getHolosHarmonics()},"holosWarmth":${paramStore.getHolosWarmth()},"holosDynamics":${paramStore.getHolosDynamics()},"holosWidth":${paramStore.getHolosWidth()},"holosGamma":${paramStore.getHolosGamma()},"holosRoomRt60":${paramStore.getHolosRoomRt60()},"holosHeadTracking":${if (paramStore.isHolosHeadTrackingEnabled()) 1.0f else 0.0f},"holosBandCount":${paramStore.getHolosBandCount()},"holosEqLow":${paramStore.getHolosEqLow()},"holosEqMid":${paramStore.getHolosEqMid()},"holosEqHigh":${paramStore.getHolosEqHigh()},"holosEqPresence":${paramStore.getHolosEqPresence()},"holosEqTiltDb":${paramStore.getHolosEqTiltDb()},"holosEqFreq":${paramStore.getHolosEqFreq()}}"""
                )
            }
            // IVANNA (v2.5.375): restaura amount/scenePull/distanceM/
            // headTracking en la Ruta A por JNI. ACTIVO por defecto
            // (amount 1.0). Sin Ruta B todavía (sin snapshot ABI). El
            // audiograma se restaura plano (0 dB); la integración con el
            // audiograma medido queda pendiente.
            runCatching {
                IvannaNativeLib.nativeSetOmniParams(
                    paramStore.getOmniAmount(), paramStore.getOmniScenePull(),
                    paramStore.getOmniDistanceM(), paramStore.isOmniHeadTrackingEnabled()
                )
                IvannaNativeLib.nativeSetOmniAudiogram(
                    FloatArray(6), FloatArray(6)
                )
            }
            // Hiper-Operador de Estado Φ (v2.5.369): restaura amount/preset/
            // XTC y la curva objetivo élite (Harman 2018 por defecto) en la
            // Ruta A por JNI. La parte daemon (Ruta B) queda pendiente: el
            // snapshot ABI v10 no tiene campos hyper_* (mismo precedente
            // que HOLOS antes de v10); Ruta B usa los defaults del efecto
            // (off) hasta el bump de ABI.
            runCatching {
                IvannaNativeLib.nativeSetHyperAmount(
                    paramStore.getHyperAmount(), paramStore.getHyperPreset(),
                    paramStore.isHyperXtcEnabled()
                )
                IvannaNativeLib.nativeSetHyperTarget(
                    paramStore.getHyperTargetKind(), paramStore.getHyperBassTrimDb()
                )
            }
            // Volterra (v2.5.369): kernel H1/H2 ACTIVO por defecto
            // (directiva: todo lo nuevo suena desde la primera canción).
            // Primero se aplica el on/off persistido (default ON) y luego
            // se carga el kernel SELECCIONADO persistido (PZ1D refinado
            // por defecto; Genezi alternativo si el usuario lo eligió en
            // CONTROL). Fail-closed: si el asset falta o la puerta lo
            // rechaza, el motor queda en bypass bit-exacto, sin
            // silenciar. El usuario puede apagarlo en CONTROL y persiste.
            runCatching {
                IvannaNativeLib.nativeSetVolterraEnabled(paramStore.isVolterraEnabled())
                IvannaNativeLib.setVolterraKernel(ctx, paramStore.getVolterraKernelId())
            }
            // Sensor acústico real (v2.5.369): arranca la captura del
            // micrófono por defecto para que Φ corra en MEDIDO (el operador
            // lee el SensorFrame vivo nativamente en cada bloque). start()
            // devuelve false sin permiso RECORD_AUDIO o si el mic falla; en
            // ese caso Φ cae a APROXIMADO (fail-closed, sin crash, sin
            // silencio). La corrección de sala en vivo (RoomCorrectionEq)
            // sigue apagada por defecto y requiere acción explícita.
            runCatching { com.ivanna.omega.audio.RealAcousticSensor.start(ctx) }
            // 4D temporal: restaura estado y profundidad (ruta A + daemon/ruta B).
            runCatching { IvannaNativeLib.nativeSetTemporalField(paramStore.isTemporalFieldEnabled(), paramStore.getTemporalFieldAmount(), paramStore.isTemporalFieldAdaptive()) }
            // Háptica de graves (4D): vive en Kotlin (ruta A), no depende de la librería nativa.
            runCatching { com.ivanna.omega.audio.BassHaptics.configure(ctx, paramStore.isBassHapticEnabled(), paramStore.getBassHapticStrength(), paramStore.getBassHapticRumble(), paramStore.getBassHapticTrimMs().toInt()) }
            runCatching { IvannaNativeLib.nativeSetSpscRingFactor(acPrefs.spscRingFactor) }
            runCatching { com.ivanna.omega.magisk.MagiskBridge.sendCommand("{\"action\":\"SET_SPSC_RING_FACTOR\",\"factor\":${acPrefs.spscRingFactor}}") }
        }

        // 2. Restaurar HarmonicExciterPrefs (perceptual state — Calibración Magistral activa por defecto)
        val hePrefs = ctx.getSharedPreferences("harmonic_exciter_prefs", Context.MODE_PRIVATE)
        val exciterEnabled = hePrefs.getBoolean("exciterEnabled", true)
        val harmonicGain = hePrefs.getFloat("harmonicGain", 0.78f)
        val antiDolby = hePrefs.getFloat("antiDolby", 0.85f)
        
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

        // Eje Supremo: Inversión Biomecánica Coclear (Cochlear-PINN)
        runCatching {
            NativeBridge.setCochlearInverseEnabled(saPrefs.cochlearInverseEnabled)
            NativeBridge.setCochlearIntensity(saPrefs.cochlearIntensity)
        }
        
        IvannaSpatialEngine.enabled = hrtfAllowed
        if (hrtfAllowed) {
            IvannaSpatialManager.setHrtfSubject(saPrefs.hrtfSubject)
        }

        // Asegurar dataset RIR extraído y registrado tanto en Non-Root como en Root
        runCatching { OmegaEngineBridge.ensureRirDataset(ctx) }

        // Restore the same validated personal snapshot used by calibration UI. No synthetic
        // room optimization, double intensity scaling or destructive reset during startup/bypass.
        val saf = com.ivanna.omega.saf.SaFEngine(ctx)
        try {
            saf.initialize().join()
        } finally {
            saf.release()
        }
        if (!safAllowed) {
            OmegaEngineBridge.pushSafLatentQ(FloatArray(7), gain = 0f)
        }

        if (saPrefs.rirEnabled && !externalStereo) {
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
            if (!externalStereo) com.ivanna.omega.spatial.WfsCalibrationManager.restore(ctx)
        }.onFailure { Log.w(TAG, "Fallo al restaurar WfsCalibrationManager: ${it.message}") }

        // 6. Cadena 4D (arco, háptica, diafonía, dosímetro). La superficie nativa retiene los valores y
        //    los reaplica cuando se crea la cadena, así que restaurar aquí es válido antes de reproducir.
        runCatching {
            if (Ivanna4DNative.isAvailable) {
                val ps = ParameterStore(ctx)
                Ivanna4DNative.nativeFourDSetLeadMs(ps.get4dLeadMs())
            runCatching { Ivanna4DNative.nativeFourDSetOutputLatencyMs(ps.get4dOutputLatencyMs()) }
                Ivanna4DNative.nativeFourDSetDepth(ps.get4dDepth())
                Ivanna4DNative.nativeFourDSetHapticScale(ps.get4dHapticScale())
                Ivanna4DNative.nativeFourDSetDosimeterMode(ps.get4dDosimeterMode())
                Ivanna4DNative.nativeFourDSetDosimeterCalibration(ps.get4dDosimeterCal())
                Ivanna4DNative.nativeFourDSetArcDepth(ps.get4dArcDepth())
                Ivanna4DNative.nativeFourDSetArc(ps.is4dArcOn())
                Ivanna4DNative.nativeFourDSetHaptics(ps.is4dHapticsOn())
                Ivanna4DNative.nativeFourDSetCrosstalk(com.ivanna.omega.audio.ExternalStereoPolicy.allowTransauralXtc(route, saPrefs.externalStereoLineOut, ps.is4dCrosstalkOn()))
                Ivanna4DNative.nativeFourDSetEnabled(ps.is4dEnabled())
            }
            FourDRouteB.push(ctx, ParameterStore(ctx), route)   // Ruta B (demonio), en hilo propio
        }.onFailure { Log.w(TAG, "Fallo al restaurar la cadena 4D: ${it.message}") }

        Log.i(TAG, "✅ Todos los controles descableados restaurados (Persistencia total conectada)")
    }
}
