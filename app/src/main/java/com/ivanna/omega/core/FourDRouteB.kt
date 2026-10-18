package com.ivanna.omega.core

import com.ivanna.omega.magisk.MagiskBridge
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicReference

/**
 * Lleva los controles 4D que viven en la Ruta B (demonio Magisk → libomega_effect.so) por el comando SET_4D:
 * 4D, arco narrativo, diafonía, profundidades y antelación. La háptica y el dosímetro son de la app y no viajan.
 *
 * El socket es bloqueante: se envía en un hilo propio y se COALESCE (si el usuario arrastra un slider solo sale
 * el último estado). Si el demonio no está, no pasa nada: la Ruta A ya tiene los mismos valores en proceso.
 */
object FourDRouteB {
    private val pending = AtomicReference<String?>(null)
    private val io = Executors.newSingleThreadExecutor { r -> Thread(r, "fourd-routeb").apply { isDaemon = true } }

    fun json(store: ParameterStore): String = String.format(
        java.util.Locale.US,
        "{\"action\":\"SET_4D\",\"fourdOn\":%d,\"arcOn\":%d,\"xtcOn\":%d,\"arcDepth\":%.4f,\"depth\":%.4f,\"leadMs\":%.3f}",
        if (store.is4dEnabled()) 1 else 0,
        if (store.is4dArcOn()) 1 else 0,
        if (store.is4dCrosstalkOn()) 1 else 0,
        store.get4dArcDepth(), store.get4dDepth(), store.get4dLeadMs()
    )

    /** Llamar tras guardar en [ParameterStore]; seguro desde el hilo de UI. */
    fun push(store: ParameterStore) {
        pending.set(json(store))
        io.execute {
            val cmd = pending.getAndSet(null) ?: return@execute   // otro envío ya llevó el último estado
            runCatching { MagiskBridge.sendCommand(cmd) }
        }
    }
}
