package com.ivanna.omega.audio

import android.content.Context
import android.content.SharedPreferences
import android.util.Log
import com.ivanna.omega.core.IvannaNativeLib
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import org.json.JSONObject

/**
 * MusicIntelligenceWorker — Orquestador Asíncrono de la Singularidad Acústica Atlas-Escena (12D).
 *
 * - Arranque en frío acelerado: primeras 3 ventanas cada 500 ms (identificación <= 1.5 s),
 *   luego régimen estacionario cada 1500 ms.
 * - Si conf <= 0.35 (gate == 0.0), la salida es identidad neutral exacta (cero invención en silencio).
 * - Sincroniza telemetría 12D, distribución posterior Bayesiana de los 12 arquetipos,
 *   hiper-vectores 3D y métricas de realismo M10 (C_t, C_s, C_d, Q, guarda activa).
 */
object MusicIntelligenceWorker {
    private const val TAG = "MusicIntelWorker"

    val STYLE_NAMES = listOf(
        "progressive_rock_70s",
        "analog_warm_60s",
        "stadium_rock_80s",
        "modern_compressed",
        "jazz_live_room",
        "electronic_dense",
        "organic_build_dynamic",
        "polymetric_complex",
        "groove_impact",
        "harmonic_dense_keys",
        "riff_texture",
        "wide_scene_studio"
    )

    val STYLE_LABELS = listOf(
        "Prog Rock 70s",
        "Analog Warm 60s",
        "Stadium Rock 80s",
        "Modern Master",
        "Jazz Live Room",
        "Electronic Dense",
        "Organic Dynamic",
        "Polymetric 7/8",
        "Groove Impact",
        "Harmonic Keys",
        "Riff Texture",
        "Wide Studio 3D"
    )

    data class Snapshot(
        val style: String = "neutral",
        val index: Int = -1,
        val confidence: Float = 0f,
        val gate: Float = 0f,
        val wfsSpread: Float = 0.5f,
        val hrtfDepth: Float = 0.5f,
        val eqTiltDb: Float = 0f,
        val dynamicsAmount: Float = 1.0f,
        val envDepth: Float = 0.0f,
        val warmth: Float = 0.5f,
        // Hiper-vectores generacionales
        val subPunch: Float = 0f,
        val vocalIntimacy: Float = 0f,
        val airHolography: Float = 0f,
        val stageElevation: Float = 0f,
        val targetIacc: Float = 0.45f,
        // Vector de características 12D + bandas Atlas
        val rms: Float = 0f,
        val crestDb: Float = 0f,
        val bassRatio: Float = 0f,
        val midRatio: Float = 0f,
        val trebleRatio: Float = 0f,
        val stereoWidth: Float = 0f,
        val transientRate: Float = 0f,
        val density: Float = 0f,
        val presenceRatio: Float = 0f,
        val airRatio: Float = 0f,
        val flatness1m: Float = 0f,
        val onsetRegularity: Float = 0.5f,
        val lraProxy12: Float = 0f,
        val sideMid: Float = 0f,
        val subBandRatio: Float = 0f,
        val bodyBandRatio: Float = 0f,
        val defBandRatio: Float = 0f,
        val lateRatio: Float = 0f,
        // Métricas de realismo M10
        val cTransient: Float = 1.0f,
        val cSpatial: Float = 1.0f,
        val cDynamic: Float = 1.0f,
        val qScore: Float = 1.0f,
        val iaccOut: Float = 0.45f,
        val guardScale: Float = 1.0f,
        val guardActive: Boolean = false,
        // Estado de conmutadores A/B en vivo
        val sceneEnabled: Boolean = true,
        val useStatDereverb: Boolean = true,
        val usePhysicalEr: Boolean = true,
        val shaperMode: Int = 1,
        val manualStyle: Int = -1,
        val userWarmth: Float = -1f,
        val maxInvGain: Float = 0.25f,
        val maxProjWet: Float = 0.25f,
        val maxExcWet: Float = 0.15f,
        val probs: List<Float> = List(12) { 1f / 12f }
    )

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private var job: Job? = null
    private val _state = MutableStateFlow(Snapshot())
    val state: StateFlow<Snapshot> = _state

    @Volatile var autoApply: Boolean = true
    @Volatile private var coldTicksRemaining: Int = 3
    @Volatile private var localUserWarmth: Float = -1f
    @Volatile private var localMaxInvGain: Float = 0.25f
    @Volatile private var localMaxProjWet: Float = 0.25f
    @Volatile private var localMaxExcWet: Float = 0.15f
    @Volatile private var prefs: SharedPreferences? = null

    @JvmOverloads
    fun start(context: Context? = null) {
        if (context != null && prefs == null) {
            runCatching {
                val sp = context.applicationContext.getSharedPreferences(
                    "ivanna_atlas_scene_prefs", Context.MODE_PRIVATE
                )
                prefs = sp
                val savedScene = sp.getBoolean("scene_enabled", true)
                val savedDereverb = sp.getBoolean("use_stat_dereverb", true)
                val savedEr = sp.getBoolean("use_physical_er", true)
                val savedShaper = sp.getInt("shaper_mode", 1)
                val savedManual = sp.getInt("manual_style", -1)
                localUserWarmth = sp.getFloat("user_warmth", -1f)
                localMaxInvGain = sp.getFloat("max_inv_gain", 0.25f)
                localMaxProjWet = sp.getFloat("max_proj_wet", 0.25f)
                localMaxExcWet = sp.getFloat("max_exc_wet", 0.15f)
                IvannaNativeLib.imeSetEnabled(savedScene)
                IvannaNativeLib.imeSetUseStatDereverb(savedDereverb)
                IvannaNativeLib.imeSetUsePhysicalEr(savedEr)
                IvannaNativeLib.imeSetShaperMode(savedShaper)
                IvannaNativeLib.imeSetManualStyleOverride(savedManual)
                IvannaNativeLib.imeSetUserWarmthOverride(localUserWarmth)
                IvannaNativeLib.imeSetMaxCeilings(localMaxInvGain, localMaxProjWet, localMaxExcWet)
            }
        }
        if (job?.isActive == true) return
        if (prefs == null) {
            // Activar desde el primer arranque (R9): SceneReconstruction ON, Lebart/Habets ON, Physical ER ON, Chebyshev ON
            IvannaNativeLib.imeSetEnabled(true)
            IvannaNativeLib.imeSetUseStatDereverb(true)
            IvannaNativeLib.imeSetUsePhysicalEr(true)
            IvannaNativeLib.imeSetShaperMode(1)
            IvannaNativeLib.imeSetMaxCeilings(localMaxInvGain, localMaxProjWet, localMaxExcWet)
        }
        coldTicksRemaining = 3

        job = scope.launch {
            var curSpread = 0.5f
            var curWarmth = 0.5f
            while (isActive) {
                try {
                    val raw = IvannaNativeLib.imeDecideNow()
                    val s = parse(raw)
                    _state.value = s

                    if (autoApply && s.sceneEnabled && s.index >= 0 && s.gate > 0.01f) {
                        curSpread += 0.25f * (s.wfsSpread - curSpread)
                        curWarmth += 0.25f * (s.warmth - curWarmth)
                        runCatching { IvannaNativeLib.nativeSetWfsSpread(curSpread) }
                        runCatching { IvannaNativeLib.nativeSetHarmonicGain(0.8f + 0.6f * curWarmth) }
                    }
                } catch (t: Throwable) {
                    Log.w(TAG, "tick error: ${t.message}")
                }
                val waitMs = if (coldTicksRemaining > 0) {
                    coldTicksRemaining--
                    500L
                } else {
                    1500L
                }
                delay(waitMs)
            }
        }
    }

    fun stop() {
        job?.cancel()
        job = null
    }

    fun setSceneReconstructionEnabled(on: Boolean) {
        IvannaNativeLib.imeSetEnabled(on)
        runCatching { prefs?.edit()?.putBoolean("scene_enabled", on)?.apply() }
        _state.value = _state.value.copy(sceneEnabled = on)
        if (!on) {
            runCatching { IvannaNativeLib.nativeSetWfsSpread(0.5f) }
            runCatching { IvannaNativeLib.nativeSetHarmonicGain(1.0f) }
        } else {
            coldTicksRemaining = 3
        }
    }

    fun setUseStatDereverb(on: Boolean) {
        IvannaNativeLib.imeSetUseStatDereverb(on)
        runCatching { prefs?.edit()?.putBoolean("use_stat_dereverb", on)?.apply() }
        _state.value = _state.value.copy(useStatDereverb = on)
    }

    fun setUsePhysicalEr(on: Boolean) {
        IvannaNativeLib.imeSetUsePhysicalEr(on)
        runCatching { prefs?.edit()?.putBoolean("use_physical_er", on)?.apply() }
        _state.value = _state.value.copy(usePhysicalEr = on)
    }

    fun setShaperMode(mode: Int) {
        val m = if (mode == 0) 0 else 1
        IvannaNativeLib.imeSetShaperMode(m)
        runCatching { prefs?.edit()?.putInt("shaper_mode", m)?.apply() }
        _state.value = _state.value.copy(shaperMode = m)
    }

    fun setManualStyleOverride(styleIdx: Int) {
        val idx = styleIdx.coerceIn(-1, 11)
        IvannaNativeLib.imeSetManualStyleOverride(idx)
        runCatching { prefs?.edit()?.putInt("manual_style", idx)?.apply() }
        coldTicksRemaining = 2
        _state.value = _state.value.copy(manualStyle = idx)
    }

    fun setUserWarmthOverride(warmthOrNeg: Float) {
        localUserWarmth = warmthOrNeg
        IvannaNativeLib.imeSetUserWarmthOverride(warmthOrNeg)
        runCatching { prefs?.edit()?.putFloat("user_warmth", warmthOrNeg)?.apply() }
        _state.value = _state.value.copy(userWarmth = warmthOrNeg)
    }

    fun setMaxCeilings(maxInvGain: Float, maxProjWet: Float, maxExcWet: Float) {
        localMaxInvGain = maxInvGain.coerceIn(0.05f, 0.50f)
        localMaxProjWet = maxProjWet.coerceIn(0.05f, 0.50f)
        localMaxExcWet = maxExcWet.coerceIn(0.02f, 0.30f)
        IvannaNativeLib.imeSetMaxCeilings(localMaxInvGain, localMaxProjWet, localMaxExcWet)
        runCatching {
            prefs?.edit()
                ?.putFloat("max_inv_gain", localMaxInvGain)
                ?.putFloat("max_proj_wet", localMaxProjWet)
                ?.putFloat("max_exc_wet", localMaxExcWet)
                ?.apply()
        }
        _state.value = _state.value.copy(
            maxInvGain = localMaxInvGain,
            maxProjWet = localMaxProjWet,
            maxExcWet = localMaxExcWet
        )
    }

    fun triggerSoftReset() {
        coldTicksRemaining = 3
        IvannaNativeLib.imeSoftReset()
    }

    private fun parse(raw: String): Snapshot = try {
        val j = JSONObject(raw)
        val probsArr = j.optJSONArray("probs")
        val parsedProbs = if (probsArr != null && probsArr.length() >= 12) {
            List(12) { idx -> probsArr.optDouble(idx, 0.0).toFloat() }
        } else {
            List(12) { 1f / 12f }
        }
        Snapshot(
            style = j.optString("style", "neutral"),
            index = j.optInt("index", -1),
            confidence = j.optDouble("confidence", 0.0).toFloat(),
            gate = j.optDouble("gate", 0.0).toFloat(),
            wfsSpread = j.optDouble("wfsSpread", 0.5).toFloat(),
            hrtfDepth = j.optDouble("hrtfDepth", 0.5).toFloat(),
            eqTiltDb = j.optDouble("eqTiltDb", 0.0).toFloat(),
            dynamicsAmount = j.optDouble("dynamicsAmount", 1.0).toFloat(),
            envDepth = j.optDouble("envDepth", 0.0).toFloat(),
            warmth = j.optDouble("warmth", 0.5).toFloat(),
            subPunch = j.optDouble("subPunch", 0.0).toFloat(),
            vocalIntimacy = j.optDouble("vocalIntimacy", 0.0).toFloat(),
            airHolography = j.optDouble("airHolography", 0.0).toFloat(),
            stageElevation = j.optDouble("stageElevation", 0.0).toFloat(),
            targetIacc = j.optDouble("targetIacc", 0.45).toFloat(),
            rms = j.optDouble("rms", 0.0).toFloat(),
            crestDb = j.optDouble("crestDb", 0.0).toFloat(),
            bassRatio = j.optDouble("bassRatio", 0.0).toFloat(),
            midRatio = j.optDouble("midRatio", 0.0).toFloat(),
            trebleRatio = j.optDouble("trebleRatio", 0.0).toFloat(),
            stereoWidth = j.optDouble("stereoWidth", 0.0).toFloat(),
            transientRate = j.optDouble("transientRate", 0.0).toFloat(),
            density = j.optDouble("density", 0.0).toFloat(),
            presenceRatio = j.optDouble("presenceRatio", 0.0).toFloat(),
            airRatio = j.optDouble("airRatio", 0.0).toFloat(),
            flatness1m = j.optDouble("flatness1m", 0.0).toFloat(),
            onsetRegularity = j.optDouble("onsetRegularity", 0.5).toFloat(),
            lraProxy12 = j.optDouble("lraProxy12", 0.0).toFloat(),
            sideMid = j.optDouble("sideMid", 0.0).toFloat(),
            subBandRatio = j.optDouble("subBandRatio", 0.0).toFloat(),
            bodyBandRatio = j.optDouble("bodyBandRatio", 0.0).toFloat(),
            defBandRatio = j.optDouble("defBandRatio", 0.0).toFloat(),
            lateRatio = j.optDouble("lateRatio", 0.0).toFloat(),
            cTransient = j.optDouble("cTransient", 1.0).toFloat(),
            cSpatial = j.optDouble("cSpatial", 1.0).toFloat(),
            cDynamic = j.optDouble("cDynamic", 1.0).toFloat(),
            qScore = j.optDouble("qScore", 1.0).toFloat(),
            iaccOut = j.optDouble("iaccOut", 0.45).toFloat(),
            guardScale = j.optDouble("guardScale", 1.0).toFloat(),
            guardActive = j.optBoolean("guardActive", false),
            sceneEnabled = j.optBoolean("sceneEnabled", true),
            useStatDereverb = j.optBoolean("useStatDereverb", true),
            usePhysicalEr = j.optBoolean("usePhysicalEr", true),
            shaperMode = j.optInt("shaperMode", 1),
            manualStyle = j.optInt("manualStyle", -1),
            userWarmth = localUserWarmth,
            maxInvGain = localMaxInvGain,
            maxProjWet = localMaxProjWet,
            maxExcWet = localMaxExcWet,
            probs = parsedProbs
        )
    } catch (_: Throwable) { Snapshot() }
}
