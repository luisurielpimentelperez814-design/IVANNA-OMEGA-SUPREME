package com.ivanna.omega.fotoholo

import android.util.Log
import com.ivanna.omega.magisk.MagiskBridge
import com.ivanna.omega.magisk.OmegaEngineBridge
import org.json.JSONObject
import java.util.Locale
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicReference
import java.util.concurrent.atomic.AtomicLong

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
    private val expectedGeneration = AtomicLong(0L)
    private val io = Executors.newSingleThreadExecutor { r -> Thread(r, "fotoholo-routeb").apply { isDaemon = true } }

    @Volatile var status: String = "sin enviar"
        private set

    fun json(width: Float): String =
        String.format(Locale.US, "{\"action\":\"SET_PERCEPTUAL_STATE\",\"spatialWidth\":%.4f}", width)

    private fun jsonLong(reply: String, key: String): Long? =
        runCatching { JSONObject(reply).optLong(key, 0L).takeIf { it > 0L } }.getOrNull()

    /** Extrae un booleano JSON de forma estricta; evita confundir status con aplicación real. */
    private fun jsonBoolean(reply: String, key: String): Boolean? {
        val match = Regex("\\\""+Regex.escape(key)+"\\\"\\s*:\\s*(true|false)", RegexOption.IGNORE_CASE)
            .find(reply) ?: return null
        return match.groupValues[1].equals("true", ignoreCase = true)
    }

    /** status=applied no basta: exigimos applied=true en la respuesta del comando. */
    fun statusFromReply(reply: String): String {
        if (reply.isBlank()) return "demonio no disponible"
        val ok = jsonBoolean(reply, "ok")
        if (ok == false) return "rechazado por el demonio"
        if (ok != true) return "respuesta sin confirmación válida del demonio"
        val dspBlockApplied = Regex("\\\"status\\\"\\s*:\\s*\\\"dsp_block_applied\\\"").containsMatchIn(reply)
        val statusApplied = Regex("\\\"status\\\"\\s*:\\s*\\\"applied\\\"").containsMatchIn(reply)
        val pendingConsumer = Regex("\\\"status\\\"\\s*:\\s*\\\"accepted_pending_consumer\\\"").containsMatchIn(reply)
        val awaitingGeneration = Regex("\\\"status\\\"\\s*:\\s*\\\"awaiting_dsp_generation\\\"").containsMatchIn(reply)
        val noActiveEffect = Regex("\\\"status\\\"\\s*:\\s*\\\"no_active_effect\\\"").containsMatchIn(reply)
        val enabledIdle = Regex("\\\"status\\\"\\s*:\\s*\\\"effect_enabled_idle\\\"").containsMatchIn(reply)
        val consumerDeclared = Regex("\\\"consumer\\\"\\s*:\\s*\\\"omega_effect\\\"").containsMatchIn(reply)
        return when {
            pendingConsumer || noActiveEffect -> "aceptado; sin consumidor activo"
            dspBlockApplied && jsonBoolean(reply, "applied") == true && consumerDeclared ->
                "aplicado por bloque DSP; generación confirmada"
            dspBlockApplied && jsonBoolean(reply, "applied") == true ->
                "aplicado por bloque DSP; consumidor confirmado"
            enabledIdle -> "efecto habilitado; esperando bloque de audio"
            awaitingGeneration -> "publicado en Ruta B; esperando generación DSP"
            dspBlockApplied -> "respuesta DSP inconsistente; read-back no confirmado"
            statusApplied && jsonBoolean(reply, "applied") == true && consumerDeclared ->
                "publicado en Ruta B; consumidor declarado, audio no verificado"
            statusApplied && jsonBoolean(reply, "applied") == true ->
                "publicado en Ruta B; sin consumidor DSP declarado"
            jsonBoolean(reply, "applied") == false ->
                "no aplicado: el consumidor DSP no confirmó la aplicación"
            statusApplied -> "estado sin confirmación explícita de aplicación"
            else -> "respuesta no reconocida"
        }
    }

    /** Delivery is complete only after the effect reports consuming this generation in a DSP block. */
    fun isDeliveryConfirmed(value: String): Boolean =
        value.startsWith("aplicado por bloque DSP;")

    /** Retry SET only on transport/protocol failure; pending consumers are polled, not republished. */
    fun shouldRetryCommand(value: String): Boolean =
        value == "demonio no disponible" ||
            value.startsWith("error:") ||
            value == "respuesta sin confirmación válida del demonio"

    /** Seguro desde cualquier hilo; ancho no finito se ignora. */
    fun push(width: Float) {
        if (!width.isFinite()) return
        pending.set(width.coerceIn(0f, 1.5f))
        io.execute {
            val w = pending.getAndSet(null) ?: return@execute
            try {
                val reply = MagiskBridge.sendCommand(json(w))
                expectedGeneration.set(jsonLong(reply, "generation") ?: 0L)
                status = statusFromReply(reply)
            } catch (e: Exception) {
                expectedGeneration.set(0L)
                status = "error: ${e.javaClass.simpleName}"
                Log.w(TAG, "envío a Ruta B falló", e)
            }
        }
    }

    /**
     * Polls GET_STATUS without publishing another SET generation. This prevents a
     * slow AudioEffect consumer from being starved by repeated identical commands.
     */
    fun refreshEvidence() {
        io.execute {
            try {
                val expected = expectedGeneration.get()
                if (expected <= 0L) {
                    status = "respuesta sin confirmación válida del demonio"
                    return@execute
                }
                val evidence = OmegaEngineBridge.requestControlPlaneEvidence()
                if (evidence == null) {
                    status = "demonio no disponible"
                    return@execute
                }
                status = when {
                    evidence.effectConsumerPresent && evidence.effectState in 1..2 &&
                        evidence.snapshotGeneration >= expected &&
                        evidence.dspAppliedGeneration >= expected ->
                        "aplicado por bloque DSP; generación confirmada" +
                            if (evidence.effectState == 1) " (efecto ahora inactivo)" else ""
                    evidence.effectState == 2 ->
                        "publicado en Ruta B; esperando generación DSP"
                    evidence.effectState == 1 ->
                        "efecto habilitado; esperando bloque de audio"
                    evidence.effectState < 0 ->
                        "evidencia del efecto no disponible"
                    else -> "aceptado; sin consumidor activo"
                }
            } catch (e: Exception) {
                status = "error: ${e.javaClass.simpleName}"
                Log.w(TAG, "consulta de evidencia Ruta B falló", e)
            }
        }
    }
}
