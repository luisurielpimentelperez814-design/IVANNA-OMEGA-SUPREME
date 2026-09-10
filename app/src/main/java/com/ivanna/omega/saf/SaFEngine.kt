package com.ivanna.omega.saf

import android.content.Context
import android.content.res.AssetManager
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.spatial.SaFOptimizer
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import java.io.File

// ── Direction descriptor ──────────────────────────────────────────────────────
enum class SaFDirection(
    val label     : String,
    val arrow     : String,
    val hint      : String,
    val azimuth   : Float,
    val elevation : Float
) {
    FRONT ("FRENTE",   "↑",  "El sonido viene de enfrente tuyo",      0f,   0f),
    RIGHT ("DERECHA",  "→",  "El sonido viene de tu lado derecho",    90f,  0f),
    LEFT  ("IZQUIERDA","←",  "El sonido viene de tu lado izquierdo", -90f,  0f),
    ABOVE ("ARRIBA",   "⬆",  "El sonido viene desde arriba",           0f,  90f),
    BEHIND("ATRÁS",    "↓",  "El sonido viene desde detrás tuyo",    180f,  0f);

    companion object {
        val ordered = listOf(FRONT, RIGHT, LEFT, ABOVE, BEHIND)
    }
}

// ── UI state ──────────────────────────────────────────────────────────────────
enum class SaFPhase { IDLE, CALIBRATING, DONE }

data class SaFState(
    val phase       : SaFPhase   = SaFPhase.IDLE,
    val iteration   : Int        = 0,
    val currentDir  : SaFDirection = SaFDirection.FRONT,
    val params      : FloatArray = FloatArray(7),
    val errorEnergy : Float      = 0f,
    val converged   : Boolean    = false,
    val jniLoaded   : Boolean    = false
)

// ── Engine ────────────────────────────────────────────────────────────────────
class SaFEngine(private val context: Context) {

    private val _state = MutableStateFlow(SaFState())
    val state: StateFlow<SaFState> = _state.asStateFlow()

    private val scope = CoroutineScope(Dispatchers.IO)

    // Reproductor de tonos binaurales de calibración HRTF (chirp 300Hz→8kHz
    // con ITD Woodworth + ILD por sombra de cabeza + rolloff pinnal para
    // diferenciar FRENTE/ATRÁS por auriculares). Hasta este parche el player
    // existía pero el motor nunca lo invocaba → encuesta ciega.
    private val player = SaFStimulusPlayer()

    // Scope dedicado para play() — SaFStimulusPlayer.play() es síncrono
    // pero el motor principal corre en IO; mezclar los dos podía encubrir
    // bloqueos del hardware bajo carga. Scope independiente y dedicado.
    private val playerScope = CoroutineScope(SupervisorJob() + Dispatchers.Default)

    // ── Init ─────────────────────────────────────────────────────────────
    fun initialize() {
        scope.launch {
            val jsonPath = resolveJsonPath()
            val loaded   = IvannaNativeLib.isLoaded
            if (loaded) {
                runCatching { SaFBridge.nativeSaFInit(jsonPath) }
                // FIX (persistencia): restaurar calibración previa si existe.
                // Sin esto, initFromJson() siempre deja q=0 (HRTF promedio)
                // aunque el usuario ya hubiera calibrado en una sesión anterior.
                val restored = runCatching { SaFBridge.nativeSaFLoadState(statePath()) }
                    .getOrDefault(false)
                // FIX (persistencia Kotlin): cargar también desde SaFCalibrationPrefs
                // (formato binario IVSF v2 con SHA-256 sobre magic+version+ts+iter
                // +converged+q[7]). Si prefs.iter > native.iter, ganamos con la
                // versión checksum-protegida — sella contra truncado/bit-flip y
                // es recuperable aunque el TXT nativo quede a medias.
                val prefsSnap = SaFCalibrationPrefs.load(context)
                val nativeIter    = if (restored) snapshot { SaFBridge.nativeSaFGetIteration() } ?: 0 else 0
                val nativeParams  = if (restored) snapshot { SaFBridge.nativeSaFGetParams() } ?: FloatArray(7) else FloatArray(7)
                val nativeConv    = if (restored) snapshot { SaFBridge.nativeSaFIsConverged() } ?: false else false
                // Resolución de divergencia: iteración más alta gana. En empate,
                // el binario sellado (prefs) tiene prioridad sobre el TXT nativo.
                val usePrefs       = prefsSnap.iteration > nativeIter
                val iter   = if (usePrefs) prefsSnap.iteration else nativeIter
                val params = if (usePrefs) prefsSnap.q        else nativeParams
                val conv   = ((if (usePrefs) prefsSnap.converged else nativeConv)) && iter > 0
                _state.value = SaFState(
                    jniLoaded   = loaded,
                    iteration   = iter,
                    params      = params,
                    converged   = conv,
                    phase       = if (conv) SaFPhase.DONE else SaFPhase.IDLE
                )
            } else {
                _state.value = SaFState(jniLoaded = loaded)
            }
        }
    }

    // ── Start / reset calibration session ────────────────────────────────
    fun startCalibration() {
        if (IvannaNativeLib.isLoaded) {
            runCatching { SaFBridge.nativeSaFReset() }
            // Reset también borra la calibración guardada — arrancamos limpio
            // a propósito, no queremos que loadState() la reviva en el próximo init().
            runCatching { SaFBridge.nativeSaFSaveState(statePath()) }
            // FIX (persistencia Kotlin): borrar también la snapshot IVSF v2
            // para que el binario sellado y el TXT nativo queden ambos en cero
            // y la siguiente init() arranque consistentemente sin calibración.
            runCatching { SaFCalibrationPrefs.clear(context) }
        }
        _state.value = SaFState(
            phase      = SaFPhase.CALIBRATING,
            currentDir = SaFDirection.ordered[0],
            jniLoaded  = IvannaNativeLib.isLoaded
        )
        // FIX (tonos): emitir el primer estímulo inmediatamente al entrar a
        // CALIBRATING. Sin esto la pantalla renderiza la flecha y el hint pero
        // no se oye nada — el botón CORRECTO/INCORRECTO se convierte en
        // encuesta ciega y Φ_SAF^∞ converge sobre ruido humano.
        playStimulus(SaFDirection.ordered[0])
    }

    /**
     * Lanza la reproducción del estímulo binaural para la dirección dada en
     * el scope dedicado. Idempotente: SaFStimulusPlayer.play() cancela el
     * estímulo anterior si quedaba algo en vuelo (AtomicInteger de
     * generación). Si el hardware no soporta float estéreo, el warning se
     * loguea y el usuario sigue pudiendo responder — útil para diagnóstico.
     */
    private fun playStimulus(direction: SaFDirection) {
        playerScope.launch {
            runCatching { player.play(direction) }
                .onFailure {
                    android.util.Log.w(TAG, "playStimulus(${direction.label}) → ${it.message}")
                }
        }
    }

    private companion object { const val TAG = "SaFEngine" }

    // ── Feed one feedback sample ──────────────────────────────────────────
    fun feedFeedback(direction: SaFDirection, correct: Boolean) {
        scope.launch {
            if (IvannaNativeLib.isLoaded) {
                runCatching { SaFBridge.nativeSaFFeedback(direction.ordinal, correct) }
            }

            // FIX: sincronizar con SaFRoomBridge — H_t real tras feedback.
            // Sin esto M_t en C++ usaba hMismatch=0 siempre; λ_t no
            // crecía aunque el usuario reportara dirección incorrecta.
            val feedbackError = if (correct) 0.2f else 1.0f
            runCatching {
                val dirErrors = FloatArray(5) { i ->
                    if (i == direction.ordinal) feedbackError else 0f
                }
                SaFOptimizer.runCalibrationStep(dirErrors)
                // syncToRoomBridge() corre dentro de runCalibrationStep() → λ_t real
            }

            val iter     = snapshot { SaFBridge.nativeSaFGetIteration() } ?: 0
            val params   = snapshot { SaFBridge.nativeSaFGetParams() }    ?: FloatArray(7)
            val energy   = snapshot { SaFBridge.nativeSaFGetError() }     ?: 0f
            val conv     = snapshot { SaFBridge.nativeSaFIsConverged() }  ?: false

            // FIX (persistencia): guardar tras cada paso, no solo al terminar.
            // Si el usuario cierra la app a mitad de calibración (5 direcciones),
            // el próximo arranque retoma desde el último paso guardado en vez
            // de perder todo el progreso y volver a q=0.
            runCatching { SaFBridge.nativeSaFSaveState(statePath()) }

            // FIX (persistencia Kotlin): mirror en SaFCalibrationPrefs (IVSF v2).
            // Cualquier kill -9 entre el save nativo y este save Kotlin no puede
            // ya corromper el binario: SHA-256 + write-then-rename con fsync.
            // Un fallo aquí NO se propaga al motor — sólo se loguea (prefs es
            // SSOT paralela, pero el TXT nativo ya está commiteado).
            runCatching {
                SaFCalibrationPrefs.save(
                    context,
                    SaFCalibrationPrefs.Snapshot(
                        q           = params,
                        iteration   = iter,
                        converged   = conv,
                        timestampMs = System.currentTimeMillis()
                    )
                )
            }.onFailure { android.util.Log.w(TAG, "prefs.save falló: ${it.message}") }

            // Advance to next direction (round-robin) or finish
            val nextIdx  = (direction.ordinal + 1) % SaFDirection.ordered.size
            val nextDir  = SaFDirection.ordered[nextIdx]
            val phase    = if (conv || iter >= 20) SaFPhase.DONE else SaFPhase.CALIBRATING

            _state.value = SaFState(
                phase       = phase,
                iteration   = iter,
                currentDir  = if (phase == SaFPhase.CALIBRATING) nextDir else direction,
                params      = params,
                errorEnergy = energy,
                converged   = conv,
                jniLoaded   = IvannaNativeLib.isLoaded
            )

            // FIX (tonos): emitir el estímulo de la siguiente dirección al
            // instante de avançar. Si ya convergió (phase=DONE), silencio.
            // playStimulus() es asíncrono en su propio scope — el motor
            // no espera al hardware.
            if (phase == SaFPhase.CALIBRATING) {
                playStimulus(nextDir)
            }
        }
    }

    /**
     * Libera recursos del motor. Llamar desde el componente Compose que
     * posea el engine (DisposableEffect). Idempotente y tolerante a fallo
     * parcial — cada cancelación va en runCatching para que una falla en el
     * JNI no impida liberar el player.
     */
    fun release() {
        runCatching { player.release() }
        runCatching { playerScope.cancel() }
        runCatching { scope.cancel() }
    }

    // ── Helpers ───────────────────────────────────────────────────────────
    private inline fun <T> snapshot(block: () -> T): T? =
        if (IvannaNativeLib.isLoaded) runCatching(block).getOrNull() else null

    /**
     * Resolves SAF_model.json path in priority order:
     *  1. Magisk deployment: /data/adb/ivanna_omega/SAF_model.json
     *  2. App internal storage (copied from APK assets on first run)
     */
    private fun resolveJsonPath(): String {
        val magiskPath = "/data/adb/ivanna_omega/SAF_model.json"
        if (File(magiskPath).exists()) return magiskPath

        val internalFile = File(context.filesDir, "saf/SAF_model.json")
        if (!internalFile.exists()) {
            internalFile.parentFile?.mkdirs()
            try {
                context.assets.open("SAF_model.json").use { inp ->
                    internalFile.outputStream().use { out -> inp.copyTo(out) }
                }
            } catch (e: Exception) {
                // Asset not bundled; optimizer will use baked constants
            }
        }
        return internalFile.absolutePath
    }

    /**
     * Ruta del archivo de estado de calibración personal (q[7] + iteración).
     * Siempre en almacenamiento interno de la app — a diferencia de
     * SAF_model.json (modelo de referencia, puede vivir en /data/adb/...),
     * esto es estado privado por usuario y no tiene motivo para salir de ahí.
     */
    private fun statePath(): String {
        val dir = File(context.filesDir, "saf")
        if (!dir.exists()) dir.mkdirs()
        return File(dir, "saf_calibration_state.txt").absolutePath
    }
}
