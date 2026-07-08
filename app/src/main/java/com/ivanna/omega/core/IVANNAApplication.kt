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

        appScope.launch {
            try {
                // 0. Persistencia del EvolutionaryKernel — DEBE fijarse ANTES
                //    de DSPBridge.init(), porque este dispara start_evo_thread()
                //    -> evo_initialize_population(), que intenta cargar el
                //    save-state en ese mismo instante.
                if (IvannaNativeLib.isLoaded) {
                    IvannaNativeLib.nativeSetEvoSavePath(
                        "${filesDir.absolutePath}/evo_population.bin"
                    )
                }

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
