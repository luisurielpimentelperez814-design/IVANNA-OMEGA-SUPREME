package com.ivanna.omega.core

import android.app.Application
import android.util.Log
import com.ivanna.omega.audio.IvannaGlobalEffectManager
import com.ivanna.omega.dsp.DSPBridge
import com.ivanna.omega.magisk.OmegaDaemon
import com.ivanna.omega.magisk.OmegaEngineBridge
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

/**
 * IVANNAApplication — Punto de entrada de la aplicación.
 *
 * FIXES DE CONECTIVIDAD:
 *   1. Expone globalEffectManager como propiedad pública para que
 *      AudioSessionReceiver pueda acceder a él via applicationContext.
 *   2. Inicializa globalEffectManager ANTES del OmegaDaemon para que
 *      las primeras sesiones de audio ya tengan efectos disponibles.
 *   3. isInitialized es Thread-safe (@Volatile).
 *   4. onTerminate() libera globalEffectManager correctamente.
 */
class IVANNAApplication : Application() {

    companion object {
        private const val TAG = "IVANNAApplication"
        val appScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
        val omegaBridge = OmegaEngineBridge()

        @Volatile
        var isInitialized = false
            private set
    }

    // FIX: expuesto como propiedad de instancia (no companion) para que
    // AudioSessionReceiver lo acceda via (context.applicationContext as IVANNAApplication)
    val globalEffectManager = IvannaGlobalEffectManager()

    override fun onCreate() {
        super.onCreate()
        Log.d(TAG, "=== IVANNA DSP Application iniciada ===")

        // FIX: OmegaEngine se inicializa con el Context ANTES del scope IO
        OmegaEngine.init(this)

        // FIX (carrera): esto DEBE ser síncrono, no ir dentro de appScope.launch.
        // MainActivity.onCreate() llama a IvannaNativeLib.nativeStartEvoThread()
        // directamente (si evo_enabled) en el hilo principal, sin esperar a
        // Application.appScope (Dispatchers.IO) — si el path se fijara ahí,
        // llegaría tarde la mayoría de las veces y evo_initialize_population()
        // correría con g_savePath vacío (persistencia deshabilitada de facto).
        if (IvannaNativeLib.isLoaded) {
            IvannaNativeLib.nativeSetEvoSavePath(
                "${filesDir.absolutePath}/evo_population.bin"
            )
        }

        appScope.launch {
            try {
                // 1. DSP nativo
                DSPBridge.init(sampleRate = 48000)
                Log.d(TAG, "✅ DSPBridge listo — 48000 Hz")

                // 2. Daemon Magisk (puede fallar sin root — no es fatal)
                val daemonOk = OmegaDaemon.start()
                Log.d(TAG, if (daemonOk) "✅ OmegaDaemon iniciado"
                           else          "⚠️ OmegaDaemon no disponible (modo no-root activo)")

                // 3. Socket bridge al daemon (esperar 300ms a que inicie)
                delay(300)
                omegaBridge.connect()
                Log.d(TAG, "✅ OmegaEngineBridge conectando en background")

                isInitialized = true
                Log.i(TAG, "✅ IVANNA-OMEGA-SUPREME lista")
            } catch (e: UnsatisfiedLinkError) {
                Log.e(TAG, "❌ Librería nativa no disponible: ${e.message}")
            } catch (e: Exception) {
                Log.e(TAG, "❌ Error de inicialización: ${e.message}")
            }
        }

        // Sync de perfiles en la nube: no-op seguro si CloudSyncManager no
        // está configurado todavía (ver CloudSyncManager.kt). Separado del
        // bloque de arriba a propósito — no debe bloquear ni afectar el
        // arranque del motor DSP si falla o tarda (red lenta, etc.).
        appScope.launch {
            try {
                CloudSyncManager.syncDown(this@IVANNAApplication, UserProfileManager(this@IVANNAApplication))
            } catch (e: Exception) {
                Log.w(TAG, "syncDown en arranque falló (no crítico): ${e.message}")
            }
        }
    }

    override fun onTerminate() {
        // Best-effort: onTerminate() no está garantizado en dispositivos reales,
        // pero el autosave periódico en evolveGeneration() ya cubre el caso de
        // que el proceso muera sin pasar por aquí.
        if (IvannaNativeLib.isLoaded) {
            try {
                IvannaNativeLib.nativeSaveEvoState()
            } catch (e: Exception) {
                Log.e(TAG, "⚠️ No se pudo guardar evo state en onTerminate: ${e.message}")
            }
        }
        globalEffectManager.releaseAll()
        omegaBridge.disconnect()
        OmegaDaemon.stop()
        super.onTerminate()
    }
}
