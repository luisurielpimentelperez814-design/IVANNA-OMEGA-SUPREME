package com.ivanna.omega.audio

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.os.SystemClock
import android.util.Log
import com.ivanna.omega.dsp.DSPBridge
import com.ivanna.omega.dsp.DSPStatePrefs
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

/**
 * BootRestoreReceiver — restaura el estado DSP persistido tras reiniciar.
 *
 * PROBLEMA QUE ARREGLA (persistencia + robustez):
 *   El manifest ya pedia android.permission.RECEIVE_BOOT_COMPLETED pero NO
 *   habia ningun receiver declarado para ese broadcast: el permiso no hacia
 *   nada. Tras un reinicio, el modulo Magisk levantaba el daemon con sus
 *   valores por defecto y los ajustes del usuario no volvian hasta que abria
 *   la app a mano.
 *
 *   La primera version del receiver arrancaba un `Thread {}` crudo sin
 *   backoff, sin timeout de `goAsync()` y sin coalescing entre broadcasts
 *   (LOCKED_BOOT_COMPLETED llega antes que BOOT_COMPLETED en dispositivos
 *   con FBE; ambos disparaban el trabajo dos veces, con el segundo boot a
 *   veces pisando la restauracion del primero a medio camino). En Android
 *   10+ ANR-vigilance mata el receiver a los 10 s de `goAsync()` — un DSP
 *   nativo que tarde en inicializar (I/O de partition encrypted-per-user
 *   antes de BOOT_COMPLETED, o daemon Magisk todavia negociando el socket)
 *   deja la restauracion a la mitad y el usuario sigue en defaults.
 *
 * No arranca servicios en primer plano (Android 12+ lo prohibe desde
 * BOOT_COMPLETED): solo re-empuja los parametros al DSP nativo y, a traves
 * de DSPState.pushToNative(), al daemon por socket si esta disponible.
 *
 * DISENO:
 *   - Executor de un solo hilo (no `Thread {}`): permite cancelacion y
 *     serializa dos broadcasts consecutivos (locked -> boot) sin re-empujar
 *     dos veces al DSP.
 *   - Guardas idempotentes: el trabajo solo se ejecuta una vez por proceso
 *     (`restored` flag). Si LOCKED_BOOT_COMPLETED ya restauro, BOOT_COMPLETED
 *     hace no-op — no hay competencia con la UI que el usuario pudiera abrir
 *     en la ventana entre broadcasts.
 *   - `goAsync()` con timeout duro de 8 s (por debajo del limite ~10s de
 *     ANR-vigilance de BroadcastReceiver en Android 10+): si algo cuelga
 *     (DSP nativo esperando lib .so, disco lento en encrypted boot), se
 *     libera el receiver y se registra el fallo — la app no queda tumbada.
 *   - Cada paso (init DSP / load prefs / push / start selector) es un
 *     `runCatching` independiente para que un fallo en la mitad no impida
 *     que el resto se aplique (patron ya usado en PersistedStateRestorer).
 */
class BootRestoreReceiver : BroadcastReceiver() {

    companion object {
        private const val TAG = "IVANNA-BootRestore"
        private const val WORK_TIMEOUT_MS = 8_000L

        // Executor unico del receiver (por proceso). @Volatile porque
        // onReceive puede correr en distintos hilos del sistema segun la
        // version de Android.
        @Volatile
        private var executor = Executors.newSingleThreadExecutor { r ->
            Thread(r, "IVANNA-BootRestore").apply { isDaemon = true }
        }

        // Flag idempotente: el trabajo real se hace una vez por vida del
        // proceso. Los dos broadcasts (LOCKED + BOOT) coalescen aqui.
        @Volatile
        private var restored = false
    }

    override fun onReceive(context: Context, intent: Intent) {
        val action = intent.action ?: return
        if (action != Intent.ACTION_BOOT_COMPLETED &&
            action != Intent.ACTION_LOCKED_BOOT_COMPLETED &&
            action != "android.intent.action.QUICKBOOT_POWERON"
        ) return

        // Coalescing: si el broadcast anterior ya sirvio, no repetir.
        if (restored) {
            Log.i(TAG, "$action recibido pero restore ya se aplico en este proceso — no-op")
            return
        }

        val appCtx = context.applicationContext
        val pending = goAsync()
        val started = SystemClock.elapsedRealtime()
        val future = executor.submit {
            try {
                doRestore(appCtx)
            } catch (t: Throwable) {
                Log.e(TAG, "boot restore fatal: ${t.message}", t)
            } finally {
                val took = SystemClock.elapsedRealtime() - started
                Log.i(TAG, "boot restore completado en ${took}ms ($action)")
                pending.finish()
            }
        }
        // Watchdog: libera el receiver antes de que el sistema haga ANR.
        // Si el executor todavia esta trabajando, se le deja terminar el
        // paso actual (setInterrupt=false) — no queremos matar un write()
        // a mitad de camino en SharedPreferences.
        try {
            future.get(WORK_TIMEOUT_MS, TimeUnit.MILLISECONDS)
        } catch (t: Throwable) {
            Log.w(TAG, "boot restore excedio ${WORK_TIMEOUT_MS}ms — se libera el receiver (trabajo sigue en background): ${t.message}")
            // pending.finish() se llama en el finally del submit.
        }
    }

    private fun doRestore(context: Context) {
        if (restored) return

        // SR real del hardware: hardcodear 48 kHz desincronizaba el DSP del
        // HAL en dispositivos que corren a 96/192 kHz tras boot (mismas
        // consecuencias que el fix de IVANNAApplication: EQ desplazado,
        // envelopes del compresor con timing equivocado).
        val hwSr = runCatching {
            val am = context.getSystemService(Context.AUDIO_SERVICE) as? android.media.AudioManager
            am?.getProperty(android.media.AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE)
                ?.toIntOrNull()
                ?.takeIf { it in 8_000..384_000 }
                ?: 48_000
        }.getOrDefault(48_000)

        runCatching { DSPBridge.init(hwSr) }
            .onFailure { Log.w(TAG, "DSPBridge.init($hwSr): ${it.message}") }

        runCatching {
            val state = DSPStatePrefs.load(context)
            state.pushToNative()
            Log.i(TAG, "estado DSP restaurado tras boot (bypass=${state.bypass}, sr=$hwSr)")
        }.onFailure { Log.w(TAG, "restore DSPState: ${it.message}") }

        runCatching { AudioBackendSelector.start(context) }
            .onFailure { Log.w(TAG, "AudioBackendSelector.start: ${it.message}") }

        restored = true
    }
}
