package com.ivanna.omega.audio

import android.util.Log
import com.ivanna.omega.core.IvannaNativeLib

object JniSafeWrapper {
    private const val TAG = "JniSafeWrapper"

    fun safeSetCompressorParams(threshold: Float, ratio: Float, attack: Float, release: Float): Boolean =
        if (!IvannaNativeLib.isLoaded) false else runCatching {
            IvannaNativeLib.nativeSetCompressorParams(
                threshold.coerceIn(-60f, 0f),
                ratio.coerceIn(1f, 20f),
                attack.coerceIn(0.1f, 500f),
                release.coerceIn(1f, 2000f)
            )
            true
        }.onFailure { Log.e(TAG, "Compressor: $it") }.getOrNull() ?: false

    fun safeSetHarmonicGain(gain: Float): Boolean =
        if (!IvannaNativeLib.isLoaded) false else runCatching {
            IvannaNativeLib.nativeSetHarmonicGain(gain.coerceIn(0f, 1f))
            true
        }.onFailure { Log.e(TAG, "Harmonic: $it") }.getOrNull() ?: false

    fun safeSetSpatialWidth(width: Float): Boolean =
        if (!IvannaNativeLib.isLoaded) false else runCatching {
            IvannaNativeLib.nativeSetSpatialWidthDirect(width.coerceIn(0f, 2f))
            true
        }.onFailure { Log.e(TAG, "Spatial: $it") }.getOrNull() ?: false

    fun safeSetEQParams(bass: Float, mid: Float, treble: Float, master: Float): Boolean =
        if (!IvannaNativeLib.isLoaded) false else runCatching {
            IvannaNativeLib.nativeSetEQParams(
                bass.coerceIn(-18f, 18f),
                mid.coerceIn(-18f, 18f),
                treble.coerceIn(-18f, 18f),
                master.coerceIn(0.1f, 2f)
            )
            true
        }.onFailure { Log.e(TAG, "EQ: $it") }.getOrNull() ?: false

    fun safeSetRouteProfile(bassDb: Float, dialogDb: Float, widener: Float): Boolean =
        if (!IvannaNativeLib.isLoaded) false else runCatching {
            AudioEngine.nativeSetRouteProfileStatic(
                bassDb.coerceIn(-18f, 18f),
                dialogDb.coerceIn(-18f, 18f),
                widener.coerceIn(0f, 3f)
            )
            true
        }.onFailure { Log.e(TAG, "Route: $it") }.getOrNull() ?: false

    fun safeSetAntiDolbyScores(speech: Float, music: Float, bass: Float): Boolean =
        if (!IvannaNativeLib.isLoaded) false else runCatching {
            val sp = speech.coerceIn(0f, 1f)
            val ba = bass.coerceIn(0f, 1f)
            val mu = music.coerceIn(0f, 1f)
            val total = sp + ba + mu
            if (total <= 0f) {
                AudioEngine.nativeSetAntiDolbyScoresStatic(0.34f, 0.33f, 0.33f)
            } else {
                AudioEngine.nativeSetAntiDolbyScoresStatic(sp / total, mu / total, ba / total)
            }
            true
        }.onFailure { Log.e(TAG, "AntiDolby: $it") }.getOrNull() ?: false
}
