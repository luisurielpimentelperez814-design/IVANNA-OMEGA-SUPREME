package com.ivannafusion

import android.app.Application
import android.util.Log
import com.ivannafusion.persistence.ParameterStore
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

class IVANNAApplication : Application() {
    companion object {
        private const val TAG = "IVANNAApplication"
        lateinit var parameterStore: ParameterStore
            private set
        val appScope   = CoroutineScope(SupervisorJob() + Dispatchers.IO)
        var isInitialized = false
            private set
        val omegaBridge = OmegaEngineBridge()
    }

    override fun onCreate() {
        super.onCreate()
        Log.d(TAG, "=== IVANNA DSP Application iniciada ===")
        parameterStore = ParameterStore(this)

        appScope.launch {
            try {
                DSPState.initialize(parameterStore)
                DSPState.detectRealHardwareCapabilities(this@IVANNAApplication)
                Log.d(TAG, "✅ DSPState listo — ${DSPState.deviceSampleRateHz} Hz")

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
