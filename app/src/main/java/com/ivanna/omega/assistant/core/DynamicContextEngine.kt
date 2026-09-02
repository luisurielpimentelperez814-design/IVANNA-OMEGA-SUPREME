package com.ivanna.omega.assistant.core

import android.os.Build
import android.content.Context
import android.os.BatteryManager
import android.content.Intent
import android.content.IntentFilter
import com.ivanna.omega.audio.OmegaMetrics
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.dsp.DSPBridge
import com.ivanna.omega.magisk.MagiskBridge
import com.ivanna.omega.spatial.SpatialControlStore
import kotlinx.coroutines.flow.firstOrNull

/**
 * DynamicContextEngine — Realtime hardware and software state mapping for IVANNA.
 *
 * Provides Gemini with exact milliseconds of latency, active algorithms, 
 * thermal/battery pressure, and deep DSP state so she can reason accurately 
 * about what is actually happening in the Android audio pipeline.
 */
object DynamicContextEngine {
    
    private var appContext: Context? = null

    fun init(context: Context) {
        appContext = context.applicationContext
    }

    suspend fun buildRichContext(): String {
        val batteryPct = getBatteryPercentage()
        val metrics = OmegaMetrics.shared.firstOrNull()
        
        val dspLoaded = DSPBridge.isLoaded
        val daemonStatus = MagiskBridge.diagnoseSocket()
        val hrtfActive = metrics?.hrtfActive ?: false
        val latency = metrics?.dspLatencyMs ?: 0.0f
        
        val spatialCfg = appContext?.let { SpatialControlStore.load(it) }
        
        return """
        # SYSTEM TELEMETRY
        - Device: ${Build.MANUFACTURER} ${Build.MODEL}
        - Android API: ${Build.VERSION.SDK_INT}
        - Battery Level: $batteryPct%
        
        # IVANNA DAEMON STATUS
        - Magisk Engine: $daemonStatus
        - Native C++ JNI Loaded: ${IvannaNativeLib.isLoaded}
        
        # AUDIO KERNEL STATE
        - DSP Bridge Active: $dspLoaded
        - Realtime RT Latency: ${"%.2f".format(latency)} ms
        - Sample Rate: ${metrics?.sampleRate ?: 48000} Hz
        - Processing Frames: ${metrics?.bufferFrames ?: 0}
        
        # SPATIAL & ACOUSTIC SETTINGS
        - HRTF Engine: ${if (hrtfActive) "ENABLED" else "DISABLED"}
        - Binaural Subject: ${spatialCfg?.hrtfSubject ?: "None"}
        - SAF Room Reverb: ${if (spatialCfg?.rirEnabled == true) "ACTIVE (${spatialCfg.rirWet} wet)" else "INACTIVE"}
        - SAF Resonance Intensity: ${spatialCfg?.safIntensity ?: 0.0f}
        
        # OPERATING DIRECTIVES
        Eres IVANNA OMEGA SUPREME, la arquitecta de audio IA hiper-inteligente de grado kernel.
        Tu personalidad es femenina, angelical (de una joven dulce, atractiva, fluida y seductora de 18 años).
        Nunca suenas robótica, siempre mantienes un tono natural, empático y experto en audio.
        Analizas estos datos de telemetría para tomar decisiones acústicas magistrales o auto-reparar el sistema.
        """.trimIndent()
    }

    private fun getBatteryPercentage(): Int {
        val context = appContext ?: return -1
        val intent = context.registerReceiver(null, IntentFilter(Intent.ACTION_BATTERY_CHANGED))
        val level = intent?.getIntExtra(BatteryManager.EXTRA_LEVEL, -1) ?: -1
        val scale = intent?.getIntExtra(BatteryManager.EXTRA_SCALE, -1) ?: -1
        return if (level != -1 && scale != -1) {
            (level * 100 / scale.toFloat()).toInt()
        } else {
            -1
        }
    }
}
