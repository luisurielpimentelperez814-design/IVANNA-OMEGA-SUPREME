package com.ivanna.omega.fotoholo

import android.content.Context
import android.content.SharedPreferences
import android.util.Log
import com.ivanna.omega.magisk.MagiskBridge
import com.ivanna.omega.magisk.OmegaEngineBridge
import java.util.Locale
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicLong
import java.util.concurrent.atomic.AtomicReference
import kotlin.math.abs

/**
 * FotoHolo's single system-wide spatial adapter.
 *
 * The active spatial owner is selected from the daemon's live snapshot:
 * WFS > HOA > Mid/Side. FotoHolo modulates the active owner only, preserves
 * the user's baseline, and restores the previous owner when arbitration changes.
 * All socket work runs on this serial control executor, never on an audio callback.
 */
object FotoHoloRouteB {
    private const val TAG = "FotoHoloRouteB"
    private const val PREFS = "ivanna_fotoholo"
    private const val BASE_WFS = "routeb_base_wfs_spread"
    private const val LAST_WFS = "routeb_last_wfs_spread"
    private const val BASE_HOA = "routeb_base_hoa_immersivity"
    private const val LAST_HOA = "routeb_last_hoa_immersivity"
    private const val OWNER = "routeb_spatial_owner"
    private const val DELTA = "routeb_last_delta"

    private val pending = AtomicReference<Pair<Float, Float>?>(null)
    private val expectedGeneration = AtomicLong(0L)
    private val io = Executors.newSingleThreadExecutor { r ->
        Thread(r, "fotoholo-routeb").apply { isDaemon = true }
    }

    @Volatile private var prefs: SharedPreferences? = null
    @Volatile private var baseWfsSpread = Float.NaN
    @Volatile private var lastAppliedWfsSpread = Float.NaN
    @Volatile private var baseHoaImmersivity = Float.NaN
    @Volatile private var lastAppliedHoaImmersivity = Float.NaN
    @Volatile private var lastOwner = "UNKNOWN"
    @Volatile private var lastDelta = 0f

    @Volatile var status: String = "sin enviar"
        private set

    fun initialize(context: Context) {
        val p = context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        prefs = p
        baseWfsSpread = p.getString(BASE_WFS, null)?.toFloatOrNull() ?: Float.NaN
        lastAppliedWfsSpread = p.getString(LAST_WFS, null)?.toFloatOrNull() ?: Float.NaN
        baseHoaImmersivity = p.getString(BASE_HOA, null)?.toFloatOrNull() ?: Float.NaN
        lastAppliedHoaImmersivity = p.getString(LAST_HOA, null)?.toFloatOrNull() ?: Float.NaN
        lastOwner = p.getString(OWNER, "UNKNOWN") ?: "UNKNOWN"
        lastDelta = p.getFloat(DELTA, 0f).takeIf { it.isFinite() } ?: 0f
    }

    private fun persistState() {
        val p = prefs ?: return
        p.edit()
            .putString(BASE_WFS, baseWfsSpread.takeIf { it.isFinite() }?.toString())
            .putString(LAST_WFS, lastAppliedWfsSpread.takeIf { it.isFinite() }?.toString())
            .putString(BASE_HOA, baseHoaImmersivity.takeIf { it.isFinite() }?.toString())
            .putString(LAST_HOA, lastAppliedHoaImmersivity.takeIf { it.isFinite() }?.toString())
            .putString(OWNER, lastOwner)
            .putFloat(DELTA, lastDelta)
            .apply()
    }

    fun json(width: Float): String =
        String.format(Locale.US, "{\"action\":\"SET_PERCEPTUAL_STATE\",\"spatialWidth\":%.4f}", width)

    /** Publishes the Route B spectral observer state; audio analysis itself runs in omega_effect. */
    fun setFotoHoloState(enabled: Boolean, intensity: Float) {
        val safeIntensity = if (intensity.isFinite()) intensity.coerceIn(0f, 1f) else 0f
        io.execute {
            try {
                val payload = String.format(
                    Locale.US,
                    "{\"action\":\"SET_FOTOHOLO_STATE\",\"enabled\":%s,\"intensity\":%.4f}",
                    if (enabled) "1" else "0",
                    safeIntensity
                )
                val reply = MagiskBridge.sendCommand(payload)
                status = if (jsonBoolean(reply, "ok") == true) {
                    statusFromReply(reply)
                } else {
                    "FotoHolo Ruta B: " + statusFromReply(reply)
                }
            } catch (e: Exception) {
                status = "FotoHolo Ruta B: error " + e.javaClass.simpleName
                Log.w(TAG, "no se pudo publicar estado FotoHolo Ruta B", e)
            }
        }
    }

    fun wfsJson(spread: Float): String =
        String.format(Locale.US, "{\"action\":\"SET_WFS\",\"wfsSpread\":%.4f}", spread)

    fun upmixJson(immersivity: Float): String =
        String.format(Locale.US, "{\"action\":\"SET_UPMIXING\",\"upmixingImmersivity\":%.4f}", immersivity)

    // Parse only scalar ACK fields. This is deterministic in local JVM tests too:
    // android.jar org.json stubs are not a reliable test runtime.
    private fun jsonScalar(reply: String, key: String): String? {
        val pattern = Regex("\"" + Regex.escape(key) +
            "\"\\s*:\\s*(true|false|null|\"((?:\\\\.|[^\"\\\\])*)\"|(-?\\d+))")
        val match = pattern.find(reply) ?: return null
        val token = match.groupValues[1]
        return when {
            token == "null" -> null
            token == "true" || token == "false" -> token
            match.groupValues[2].isNotEmpty() ->
                match.groupValues[2].replace("\\\\\"", "\"").replace("\\\\\\\\", "\\\\")
            else -> token
        }
    }

    private fun jsonLong(reply: String, key: String): Long? =
        jsonScalar(reply, key)?.toLongOrNull()?.takeIf { it > 0L }

    private fun jsonBoolean(reply: String, key: String): Boolean? =
        when (jsonScalar(reply, key)) {
            "true" -> true
            "false" -> false
            else -> null
        }

    fun statusFromReply(reply: String): String {
        if (reply.isBlank()) return "demonio no disponible"
        if (jsonScalar(reply, "ok") == null) {
            return "respuesta sin confirmación válida del demonio"
        }
        if (jsonBoolean(reply, "ok") != true) return "rechazado por el demonio"
        val statusValue = jsonScalar(reply, "status").orEmpty()
        val applied = jsonBoolean(reply, "applied")
        val consumerDeclared = jsonScalar(reply, "consumer") == "omega_effect"
        return when (statusValue) {
            "accepted_pending_consumer", "no_active_effect" ->
                "aceptado; sin consumidor activo"
            "dsp_block_applied" -> if (applied == true) {
                "bloque DSP completado; verificando propietario espacial"
            } else {
                "respuesta DSP inconsistente; read-back no confirmado"
            }
            "effect_enabled_idle" -> "efecto habilitado; esperando bloque de audio"
            "awaiting_dsp_generation" -> "publicado en Ruta B; esperando generación DSP"
            "applied" -> when {
                applied == true && consumerDeclared ->
                    "publicado en Ruta B; consumidor declarado, audio no verificado"
                applied == true -> "publicado en Ruta B; sin consumidor DSP declarado"
                applied == false -> "no aplicado: el consumidor DSP no confirmó la aplicación"
                else -> "estado sin confirmación explícita de aplicación"
            }
            else -> if (applied == false) {
                "no aplicado: el consumidor DSP no confirmó la aplicación"
            } else {
                "respuesta no reconocida"
            }
        }
    }

    fun isDeliveryConfirmed(value: String): Boolean =
        value.startsWith("aplicado por bloque DSP; spread WFS consumido") ||
            value.startsWith("aplicado por bloque DSP; inmersividad HOA consumida") ||
            value.startsWith("aplicado por bloque DSP; anchura M/S activa")

    fun shouldRetryCommand(value: String): Boolean =
        value == "demonio no disponible" ||
            value.startsWith("error:") ||
            value == "respuesta sin confirmación válida del demonio" ||
            value == "arbitraje espacial no verificable" ||
            value == "propietario cambió; reaplicación necesaria"

    private fun updateBaselinesFromExternalChanges(e: OmegaEngineBridge.ControlPlaneEvidence): Boolean {
        var changed = false
        if (e.wfsSpread.isFinite()) {
            if (!baseWfsSpread.isFinite()) {
                baseWfsSpread = e.wfsSpread
                changed = true
            } else if ((lastAppliedWfsSpread.isFinite() &&
                    abs(e.wfsSpread - lastAppliedWfsSpread) > 0.01f) ||
                (!lastAppliedWfsSpread.isFinite() && lastOwner == "WFS" &&
                    abs(e.wfsSpread - baseWfsSpread) > 0.01f)) {
                // A real WFS slider change becomes the new user baseline, not accumulated FotoHolo drift.
                baseWfsSpread = e.wfsSpread
                lastAppliedWfsSpread = Float.NaN
                changed = true
            }
        }
        if (e.upmixingImmersivity.isFinite()) {
            val current = e.upmixingImmersivity
            if (!baseHoaImmersivity.isFinite()) {
                baseHoaImmersivity = current
                changed = true
            } else if ((lastAppliedHoaImmersivity.isFinite() &&
                    abs(current - lastAppliedHoaImmersivity) > 0.01f) ||
                (!lastAppliedHoaImmersivity.isFinite() && lastOwner == "HOA" &&
                    abs(current - baseHoaImmersivity) > 0.01f)) {
                baseHoaImmersivity = current
                lastAppliedHoaImmersivity = Float.NaN
                changed = true
            }
        }
        if (changed) persistState()
        return changed
    }

    private fun restorePreviousOwnerIfNeeded(owner: String): Boolean {
        if (lastOwner == "WFS" && owner != "WFS" && baseWfsSpread.isFinite()) {
            val reply = MagiskBridge.sendCommand(wfsJson(baseWfsSpread.coerceIn(0f, 2f)))
            if (jsonBoolean(reply, "ok") != true) {
                status = statusFromReply(reply)
                return false
            }
            lastAppliedWfsSpread = baseWfsSpread.coerceIn(0f, 2f)
            expectedGeneration.set(jsonLong(reply, "generation") ?: 0L)
        }
        if (lastOwner == "HOA" && owner != "HOA" && baseHoaImmersivity.isFinite()) {
            val reply = MagiskBridge.sendCommand(upmixJson(baseHoaImmersivity))
            if (jsonBoolean(reply, "ok") != true) {
                status = statusFromReply(reply)
                return false
            }
            lastAppliedHoaImmersivity = baseHoaImmersivity
            expectedGeneration.set(jsonLong(reply, "generation") ?: 0L)
        }
        return true
    }

    /** Coalesced system-wide update; the route owner is read from the daemon, not guessed. */
    fun push(width: Float, baseWidth: Float) {
        if (!width.isFinite() || !baseWidth.isFinite()) return
        pending.set(width.coerceIn(0f, 1.5f) to baseWidth.coerceIn(0f, 1.5f))
        io.execute {
            val (safeWidth, safeBase) = pending.getAndSet(null) ?: return@execute
            try {
                val evidence = OmegaEngineBridge.requestControlPlaneEvidence()
                if (evidence == null) {
                    expectedGeneration.set(0L)
                    status = "demonio no disponible"
                    return@execute
                }
                val owner = evidence.spatialOwner
                if (owner == null ||
                    (owner == "WFS" && !evidence.wfsSpread.isFinite()) ||
                    (owner == "HOA" && !evidence.upmixingImmersivity.isFinite())) {
                    expectedGeneration.set(0L)
                    status = "arbitraje espacial no verificable"
                    return@execute
                }

                updateBaselinesFromExternalChanges(evidence)
                if (!restorePreviousOwnerIfNeeded(owner)) return@execute

                val delta = (safeWidth - safeBase).coerceIn(-0.25f, 0.25f)
                val payload = when (owner) {
                    "WFS" -> {
                        if (!baseWfsSpread.isFinite()) {
                            status = "arbitraje espacial no verificable"
                            return@execute
                        }
                        val target = if (abs(delta) < 0.001f) baseWfsSpread.coerceIn(0f, 2f)
                            else (baseWfsSpread + delta * 0.5f).coerceIn(0.5f, 2f)
                        wfsJson(target).also { lastAppliedWfsSpread = target }
                    }
                    "HOA" -> {
                        if (!baseHoaImmersivity.isFinite()) {
                            status = "arbitraje espacial no verificable"
                            return@execute
                        }
                        val effectiveBase = baseHoaImmersivity.coerceIn(0f, 1f)
                        val target = if (abs(delta) < 0.001f) baseHoaImmersivity
                            else (effectiveBase + delta * 0.25f).coerceIn(0f, 1f)
                        upmixJson(target).also { lastAppliedHoaImmersivity = target }
                    }
                    else -> json(safeWidth)
                }

                val reply = MagiskBridge.sendCommand(payload)
                if (jsonBoolean(reply, "ok") != true) {
                    expectedGeneration.set(0L)
                    status = statusFromReply(reply)
                    persistState()
                    return@execute
                }
                expectedGeneration.set(jsonLong(reply, "generation") ?: 0L)
                lastOwner = owner
                lastDelta = delta
                status = statusFromReply(reply)
                persistState()
            } catch (e: Exception) {
                expectedGeneration.set(0L)
                status = "error: " + e.javaClass.simpleName
                Log.w(TAG, "envío a Ruta B falló", e)
            }
        }
    }

    /** Polls the effect beacon without publishing a new generation. */
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
                val owner = evidence.spatialOwner
                if (owner == null ||
                    (owner == "WFS" && !evidence.wfsSpread.isFinite()) ||
                    (owner == "HOA" && !evidence.upmixingImmersivity.isFinite())) {
                    status = "arbitraje espacial no verificable"
                    return@execute
                }
                val baselineChanged = updateBaselinesFromExternalChanges(evidence)
                if (owner != lastOwner || (baselineChanged && abs(lastDelta) > 0.001f)) {
                    status = "propietario cambió; reaplicación necesaria"
                    return@execute
                }
                status = when {
                    evidence.snapshotGeneration >= expected &&
                        evidence.dspAppliedGeneration >= expected -> {
                        val ownerLabel = when (owner) {
                            "WFS" -> "spread WFS consumido"
                            "HOA" -> "inmersividad HOA consumida"
                            else -> "anchura M/S activa"
                        }
                        "aplicado por bloque DSP; " + ownerLabel +
                            if (evidence.effectState == 2) "" else " (último bloque ya finalizó)"
                    }
                    evidence.effectState == 2 -> "publicado en Ruta B; esperando generación DSP"
                    evidence.effectState == 1 -> "efecto habilitado; esperando bloque de audio"
                    else -> "aceptado; sin consumidor activo"
                }
            } catch (e: Exception) {
                status = "error: " + e.javaClass.simpleName
                Log.w(TAG, "consulta de evidencia Ruta B falló", e)
            }
        }
    }
}
