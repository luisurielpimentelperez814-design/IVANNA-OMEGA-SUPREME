package com.ivanna.omega.audio

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.util.Log
import com.ivanna.omega.dsp.DSPBridge
import com.ivanna.omega.dsp.DSPStatePrefs

/**
 * BootRestoreReceiver — restaura el estado DSP persistido tras reiniciar.
 *
 * PROBLEMA QUE ARREGLA (persistencia):
 *   El manifest ya pedia android.permission.RECEIVE_BOOT_COMPLETED pero NO
 *   habia ningun receiver declarado para ese broadcast: el permiso no hacia
 *   nada. Tras un reinicio, el modulo Magisk levantaba el daemon con sus
 *   valores por defecto y los ajustes del usuario no volvian hasta que abria
 *   la app a mano.
 *
 * No arranca servicios en primer plano (Android 12+ lo prohibe desde
 * BOOT_COMPLETED): solo re-empuja los parametros al DSP nativo y, a traves
 * de DSPState.pushToNative(), al daemon por socket si esta disponible.
 */
class BootRestoreReceiver : BroadcastReceiver() {

    override fun onReceive(context: Context, intent: Intent) {
        val action = intent.action ?: return
        if (action != Intent.ACTION_BOOT_COMPLETED &&
            action != Intent.ACTION_LOCKED_BOOT_COMPLETED &&
            action != "android.intent.action.QUICKBOOT_POWERON"
        ) return

        val pending = goAsync()
        Thread {
            try {
                // FIX (hi-res): SR hardcodeado a 48 kHz desincronizaba el DSP
                // del HAL en dispositivos que corren a 96/192 kHz tras boot —
                // mismas consecuencias que el fix de IVANNAApplication (EQ
                // desplazado, envelopes del compresor con timing equivocado).
                val hwSr = (context.getSystemService(Context.AUDIO_SERVICE) as android.media.AudioManager)
                    .getProperty(android.media.AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE)
                    ?.toIntOrNull() ?: 48000
                runCatching { DSPBridge.init(hwSr) }
                val state = DSPStatePrefs.load(context)
                runCatching { state.pushToNative() }
                Log.i("IVANNA-BootRestore", "estado DSP restaurado tras boot (bypass=${state.bypass})")
                // Backend correcto para este arranque (root vs sin root).
                runCatching { AudioBackendSelector.start(context) }
            } finally {
                pending.finish()
            }
        }.start()
    }
}
