package com.ivanna.omega.fotoholo

import android.util.Log
import com.ivanna.omega.magisk.MagiskBridge
import java.util.Locale
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicReference

/**
 * Lleva el ancho que decide Fotoholografía a la Ruta B (demonio Magisk → libomega_effect.so), igual que
 * [com.ivanna.omega.core.FourDRouteB] hace con la cadena 4D. Antes el lazo de ancho solo movía la Ruta A
 * (`nativeSetSpatialWidthDirect` en proceso): con el efecto global activo no cambiaba nada.
 *
 * Usa SET_PERCEPTUAL_STATE solo con `spatialWidth`: el demonio conserva el resto del estado cuando falta
 * un campo. El socket es bloqueante → hilo propio y envío COALESCIDO (solo sale el último ancho).
 * El resultado real (aplicado / pendiente / demonio ausente) se publica en [status]; no se asume.
 */
object FotoHoloRouteB {
    private const val TAG = "FotoHoloRouteB"
    private val pending = AtomicReference<Float?>(null)
    private val io = Executors.newSingleThreadExecutor { r -> Thread(r, "fotoholo-routeb").apply { isDaemon = true } }

    @Volatile var status: String = "sin enviar"
        private set

    fun json(width: Float): String =
        String.format(Locale.US, "{\"action\":\"SET_PERCEPTUAL_STATE\",\"spatialWidth\":%.4f}", width)

    /** Interpreta la respuesta del demonio; "" = no contestó. Puro, para probarlo en JVM. */
    fun statusFromReply(reply: String): String = when {
        reply.isEmpty() -> "demonio no disponible"
        reply.contains("\"ok\":false") -> "rechazado por el demonio"
        reply.contains("accepted_pending_consumer") -> "aceptado; sin consumidor activo"
        reply.contains("\"status\":\"applied\"") -> "aplicado en Ruta B"
        else -> "respuesta no reconocida"
    }

    /** Seguro desde cualquier hilo; ancho no finito se ignora. */
    fun push(width: Float) {
        if (!width.isFinite()) return
        pending.set(width.coerceIn(0f, 1.5f))
        io.execute {
            val w = pending.getAndSet(null) ?: return@execute   // otro envío ya llevó el último valor
            try {
                status = statusFromReply(MagiskBridge.sendCommand(json(w)))
            } catch (e: Exception) {
                status = "error: ${e.javaClass.simpleName}"
                Log.w(TAG, "envío a Ruta B falló", e)
            }
        }
    }
}
