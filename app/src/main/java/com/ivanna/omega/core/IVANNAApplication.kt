package com.ivannafusion

import android.app.Application
import android.util.Log
import com.ivanna.omega.dsp.DSPBridge
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

class IVANNAApplication : Application() {

    companion object {
        private const val TAG = "IVANNAApplication"
        val appScope    = CoroutineScope(SupervisorJob() + Dispatchers.IO)
        var isInitialized = false
            private set
        val omegaBridge = OmegaEngineBridge()
    }

    override fun onCreate() {
        super.onCreate()
        Log.d(TAG, "=== IVANNA DSP Application iniciada ===")

        appScope.launch {
            try {
                // Inicializar DSP nativo
                DSPBridge.init(sampleRate = 48000)
                Log.d(TAG, "✅ DSPBridge listo — 48000 Hz")

                val daemonOk = OmegaDaemon.start()
                Log.d(TAG, if (daemonOk) "✅ OmegaDaemon iniciado"
                           else          "⚠️ OmegaDaemon no disponible (Magisk standalone activo?)")

                delay(300)
                omegaBridge.connect()
                Log.d(TAG, "✅ OmegaEngineBridge conectando en background")

                isInitialized = true
                Log.i(TAG, "✅ IVANNA-FUSION lista")
            } catch (e: UnsatisfiedLinkError) {
                Log.e(TAG, "❌ Librería nativa: ${e.message}")
            } catch (e: Exception) {
                Log.e(TAG, "❌ Init: ${e.message}")
            }
        }
    }

    override fun onTerminate() {
        omegaBridge.disconnect()
        OmegaDaemon.stop()
        super.onTerminate()
    }
}
