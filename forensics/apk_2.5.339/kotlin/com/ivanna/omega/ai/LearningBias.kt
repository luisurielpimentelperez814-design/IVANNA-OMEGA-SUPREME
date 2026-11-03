package com.ivanna.omega.ai

import android.content.Context
import android.util.Log
import java.io.File
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicBoolean
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch

/**
 * LearningBias — Ruta A del prompt "aprendizaje en tiempo real":
 * media movil exponencial del delta (usuario - autonomo) por
 * (contexto=genero_o_preset, param_key). Auditable con `adb pull`
 * (formato JSON-lines append-only en filesDir/learning_bias.jsonl).
 *
 * POR QUE ESTA CLASE (y no AdaptiveLearning/AIInferenceEngine/ModelManager):
 *   Esas tres clases nunca se instancian, AIInferenceEngine solo multiplica
 *   por 1.05 y ModelManager escribe un .txt disfrazado de .tflite.
 *   No se borran (regla de oro), pero no se pueden usar para "aprender"
 *   nada real. LearningBias es la contraparte determinista y verificable
 *   que si se conecta al UnifiedControlFrame -> control_apply_frame().
 *
 * PUNTO DE APLICACION (single source of truth):
 *   El C++ lee el sesgo via `LearningBias.jniGetBiasForActiveContext(param)`
 *   desde control_apply_frame() (hilo de control, NUNCA audio thread).
 *   Se suma al valor propuesto antes de publicar ControlFrame al bus.
 *   Es el mismo camino que ya usa el genoma evolutivo -> no se crea
 *   una ruta paralela.
 */
class LearningBias(private val context: Context) {

    companion object {
        private const val TAG = "IVANNA.LearningBias"
        private const val FILE_NAME = "learning_bias.jsonl"
        private const val EMA_ALPHA = 0.15f          // paso de la media movil
        private const val MAX_ABS_BIAS = 0.5f        // clamp anti-runaway
        private const val PERSIST_DEBOUNCE_MS = 1500L

        @Volatile private var INSTANCE: LearningBias? = null

        /** Punto de entrada JNI: lo llama control_apply_frame() en C++. */
        @JvmStatic
        fun jniGetBiasForActiveContext(paramKey: String): Float {
            val inst = INSTANCE ?: return 0f
            return inst.getBiasForContext(inst.currentContext, paramKey)
        }

        /** Setter opcional del contexto desde C++/UI si se detecta genero nuevo. */
        @JvmStatic
        fun jniSetActiveContext(ctx: String) {
            INSTANCE?.setActiveContext(ctx)
        }
    }

    // ── State ──────────────────────────────────────────────────────────────
    // Estructura: biases[context][paramKey] -> Float bias (delta acumulado).
    private val biases = ConcurrentHashMap<String, ConcurrentHashMap<String, Float>>()

    @Volatile private var currentContext: String = "preset:Flat"
    private val dirty = AtomicBoolean(false)
    private val scope = CoroutineScope(Dispatchers.IO + SupervisorJob())
    private var persistJob: Job? = null
    private val fileLock = Any()

    init {
        INSTANCE = this
    }

    fun setActiveContext(ctx: String) {
        if (ctx.isBlank()) return
        currentContext = ctx
    }

    /**
     * Devuelve el sesgo aprendido para (context, paramKey). Cero si no hay
     * experiencia previa. Es lookup lock-free (ConcurrentHashMap).
     */
    fun getBiasForContext(context: String, paramKey: String): Float {
        val inner = biases[context] ?: return 0f
        return inner[paramKey] ?: 0f
    }

    /**
     * Captura una correccion del usuario. Actualiza el bias EMA para el
     * contexto/param, persiste asincronamente y publica el contexto activo
     * al C++ para que control_apply_frame() lo lea en la siguiente pasada.
     *
     * @param ctx contexto de detection ("genre:rock", "preset:Warm", ...).
     * @param paramKey identificador del parametro ("nho_harmonic", ...).
     * @param autonomous valor propuesto por el motor autonomo (o null si no aplica).
     * @param userValue valor final que puso el usuario.
     */
    fun captureCorrection(
        ctx: String,
        paramKey: String,
        autonomous: Float?,
        userValue: Float
    ) {
        if (ctx.isBlank()) return
        setActiveContext(ctx)

        // Solo aprendemos si hab a un valor autonomo del que separarse.
        // Sin autonomo, la correccion es un ajuste manual puro -> se registra
        // pero NO se acumula en el bias (evita "aprender" preferencias
        // que no responden a nada que el motor propuso).
        val delta = if (autonomous != null) (userValue - autonomous) else 0f

        val inner = biases.getOrPut(ctx) { ConcurrentHashMap() }
        val prev = inner[paramKey] ?: 0f
        val next = if (autonomous != null) {
            val ema = prev + EMA_ALPHA * (delta - prev)
            ema.coerceIn(-MAX_ABS_BIAS, MAX_ABS_BIAS)
        } else prev
        inner[paramKey] = next

        // FIX (bloqueo del hilo principal): captureCorrection() se llama desde
        // los onValueChange de los sliders en MainActivity, que disparan en
        // CADA tick del gesto de arrastre (decenas de veces por segundo) en
        // el hilo de UI. appendJsonLine() hace I/O de disco sincrono
        // (File.appendText dentro de un synchronized) - antes se ejecutaba
        // ahi mismo, en el hilo principal, en cada tick. Eso traba/entrecorta
        // cualquier slider del DSP al moverlo. El calculo de la EMA (rapido,
        // in-memory, ConcurrentHashMap) se queda sincrono; solo el disk I/O
        // se despacha al scope de IO ya existente en esta clase.
        scope.launch { appendJsonLine(ctx, paramKey, autonomous, userValue, delta, next) }
        scheduleFlush()

        Log.i(TAG, "capture ctx=$ctx param=$paramKey user=$userValue auto=$autonomous " +
                "delta=$delta bias=$prev->$next")
    }

    /** Carga el estado desde disco (formato JSON-lines). Idempotente. */
    fun load() {
        val f = File(context.filesDir, FILE_NAME)
        if (!f.exists()) return
        try {
            // Sumamos por delta con la misma EMA para que la carga produzca
            // exactamente el mismo estado que la sesion original.
            val perContext = HashMap<String, HashMap<String, Float>>()
            f.forEachLine { line ->
                val ctx = extractJsonString(line, "ctx") ?: return@forEachLine
                val param = extractJsonString(line, "param") ?: return@forEachLine
                val bias = extractJsonFloat(line, "bias")
                if (bias != null) {
                    // Tomamos el ultimo bias registrado por par (ctx,param) como
                    // fuente de verdad -> equivale a "replay hasta el ultimo".
                    perContext.getOrPut(ctx) { HashMap() }[param] = bias
                }
            }
            for ((ctx, m) in perContext) {
                val inner = biases.getOrPut(ctx) { ConcurrentHashMap() }
                for ((k, v) in m) inner[k] = v
            }
            Log.i(TAG, "load: ${perContext.size} contexts restored")
        } catch (t: Throwable) {
            Log.w(TAG, "load fallo", t)
        }
    }

    fun release() {
        try { persistJob?.cancel() } catch (_: Throwable) {}
        try { scope.cancel() } catch (_: Throwable) {}
        INSTANCE = null
    }

    // ── Persistence ────────────────────────────────────────────────────────
    private fun appendJsonLine(
        ctx: String, paramKey: String, autonomous: Float?,
        userValue: Float, delta: Float, bias: Float
    ) {
        val ts = System.currentTimeMillis()
        val autoStr = autonomous?.toString() ?: "null"
        val line = "{\"ts\":" + ts +
            ",\"ctx\":" + quote(ctx) +
            ",\"param\":" + quote(paramKey) +
            ",\"user\":" + userValue +
            ",\"auto\":" + autoStr +
            ",\"delta\":" + delta +
            ",\"bias\":" + bias + "}\n"
        synchronized(fileLock) {
            try {
                File(context.filesDir, FILE_NAME).appendText(line)
            } catch (t: Throwable) {
                Log.w(TAG, "append fallo", t)
            }
        }
        dirty.set(true)
    }

    private fun scheduleFlush() {
        // El append-per-line ya es persistencia inmediata; este job existe
        // solo para futuros consumidores (rotacion / snapshot binario).
        if (persistJob?.isActive == true) return
        persistJob = scope.launch {
            kotlinx.coroutines.delay(PERSIST_DEBOUNCE_MS)
            dirty.set(false)
        }
    }

    private fun quote(s: String): String =
        "\"" + s.replace("\\", "\\\\").replace("\"", "\\\"") + "\""

    private fun extractJsonString(line: String, key: String): String? {
        val needle = "\"" + key + "\":\""
        val i = line.indexOf(needle)
        if (i < 0) return null
        val start = i + needle.length
        val end = line.indexOf('"', start)
        if (end < 0) return null
        return line.substring(start, end).replace("\\\"", "\"").replace("\\\\", "\\")
    }

    private fun extractJsonFloat(line: String, key: String): Float? {
        val needle = "\"" + key + "\":"
        val i = line.indexOf(needle)
        if (i < 0) return null
        var j = i + needle.length
        val end = line.length
        val sb = StringBuilder()
        while (j < end) {
            val c = line[j]
            if (c == ',' || c == '}' || c == ' ') break
            sb.append(c); j++
        }
        val raw = sb.toString()
        if (raw == "null" || raw.isEmpty()) return null
        return raw.toFloatOrNull()
    }
}
