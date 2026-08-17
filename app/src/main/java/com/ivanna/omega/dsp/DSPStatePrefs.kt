package com.ivanna.omega.dsp

import android.content.Context

object DSPStatePrefs {
    private const val PREFS_NAME = "ivanna_dsp_state"

    fun load(context: Context): DSPState {
        val p = context.applicationContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        val default = DSPState()
        return DSPState(
            drive          = p.getFloat("drive", default.drive),
            wet            = p.getFloat("wet", default.wet),
            mix            = p.getFloat("mix", default.mix),
            alpha          = p.getFloat("alpha", default.alpha),
            beta           = p.getFloat("beta", default.beta),
            gamma          = p.getFloat("gamma", default.gamma),
            freq           = p.getFloat("freq", default.freq),
            resonance      = p.getFloat("resonance", default.resonance),
            low            = p.getFloat("low", default.low),
            mid            = p.getFloat("mid", default.mid),
            high           = p.getFloat("high", default.high),
            presence       = p.getFloat("presence", default.presence),
            master         = p.getFloat("master", default.master),
            compThreshold  = p.getFloat("compThreshold", default.compThreshold),
            compRatio      = p.getFloat("compRatio", default.compRatio),
            exciterDrive   = p.getFloat("exciterDrive", default.exciterDrive),
            stereoWidth    = p.getFloat("stereoWidth", default.stereoWidth),
            makeupGain     = p.getFloat("makeupGain", default.makeupGain),
            bypass         = p.getBoolean("bypass", default.bypass)
        )
    }

    fun save(context: Context, state: DSPState) {
        val p = context.applicationContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        p.edit()
            .putFloat("drive", state.drive)
            .putFloat("wet", state.wet)
            .putFloat("mix", state.mix)
            .putFloat("alpha", state.alpha)
            .putFloat("beta", state.beta)
            .putFloat("gamma", state.gamma)
            .putFloat("freq", state.freq)
            .putFloat("resonance", state.resonance)
            .putFloat("low", state.low)
            .putFloat("mid", state.mid)
            .putFloat("high", state.high)
            .putFloat("presence", state.presence)
            .putFloat("master", state.master)
            .putFloat("compThreshold", state.compThreshold)
            .putFloat("compRatio", state.compRatio)
            .putFloat("exciterDrive", state.exciterDrive)
            .putFloat("stereoWidth", state.stereoWidth)
            .putFloat("makeupGain", state.makeupGain)
            .putBoolean("bypass", state.bypass)
            .apply()
    }
}
