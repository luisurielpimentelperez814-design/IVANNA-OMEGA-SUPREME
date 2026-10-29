package com.ivanna.omega.saf

import android.content.Context
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.spatial.SaFOptimizer
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger
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
    val jniLoaded   : Boolean    = false,
    val modelLoaded : Boolean    = false,
    val stimulusReady: Boolean   = false,
    val message: String          = "",
    val deliveryMessage: String  = "Ruta global sin confirmar"
)

// ── Engine ────────────────────────────────────────────────────────────────────
class SaFEngine(private val context: Context) {
    companion object {
        private const val TAG = "SaFEngine"
        @Volatile private var latestStateSnapshot: SaFState? = null

        /** Last state emitted by an actual SaFEngine instance; null until one initializes. */
        fun latestTelemetry(): SaFState? = latestStateSnapshot?.let {
            it.copy(params = it.params.copyOf())
        }
    }

    private fun publishLatestState() {
        latestStateSnapshot = _state.value.copy(params = _state.value.params.copyOf())
    }

    private val _state = MutableStateFlow(SaFState())
    val state: StateFlow<SaFState> = _state.asStateFlow()

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO.limitedParallelism(1))
    private val feedbackPending = AtomicBoolean(false)
    private val stimulusGeneration = AtomicInteger(0)

    // Reproductor de tonos binaurales de calibración HRTF (chirp 300Hz→8kHz
    // con ITD Woodworth + ILD por sombra de cabeza + rolloff pinnal para
    // diferenciar FRENTE/ATRÁS por auriculares). Hasta este parche el player
    // existía pero el motor nunca lo invocaba → encuesta ciega.
    private val player = SaFStimulusPlayer()

    // Scope dedicado para play() — SaFStimulusPlayer.play() es síncrono
    // pero el motor principal corre en IO; mezclar los dos podía encubrir
    // bloqueos del hardware bajo carga. Scope independiente y dedicado.
    private val playerScope = CoroutineScope(SupervisorJob() + Dispatchers.Default.limitedParallelism(1))

    // ── Init ─────────────────────────────────────────────────────────────
    fun initialize() = scope.launch {
        val jsonPath = resolveJsonPath()
        val loaded   = IvannaNativeLib.isLoaded
        if (loaded) {
            val hasLiveCalibration = (snapshot { SaFBridge.nativeSaFGetIteration() } ?: 0) > 0 &&
                (snapshot { SaFBridge.nativeSaFGetStatus() }?.getOrNull(7) ?: 0f) >= 0.5f
            if (!hasLiveCalibration) runCatching { SaFBridge.nativeSaFInit(jsonPath) }
            // FIX (persistencia): restaurar calibración previa si existe.
            // Sin esto, initFromJson() siempre deja q=0 (HRTF promedio)
            // aunque el usuario ya hubiera calibrado en una sesión anterior.
            val restored = hasLiveCalibration || runCatching { SaFBridge.nativeSaFLoadState(statePath()) }
                .getOrDefault(false)
            // FIX (persistencia Kotlin): cargar también desde SaFCalibrationPrefs
            // (formato binario IVSF v2 con SHA-256 sobre magic+version+ts+iter
            // +converged+q[7]). Si prefs.iter > native.iter, ganamos con la
            // versión checksum-protegida — sella contra truncado/bit-flip y
            // es recuperable aunque el TXT nativo quede a medias.
            val prefsSnap = SaFCalibrationPrefs.load(context)
            val safStatus     = snapshot { SaFBridge.nativeSaFGetStatus() }
            val statusParams  = if (safStatus != null && safStatus.size >= 7) safStatus.copyOfRange(0, 7) else null
            val modelOk       = safStatus != null && safStatus.size >= 8 && safStatus[7] >= 0.5f
            val nativeIter    = if (restored) snapshot { SaFBridge.nativeSaFGetIteration() } ?: 0 else 0
            val nativeParams  = if (restored) (snapshot { SaFBridge.nativeSaFGetParams() } ?: statusParams ?: FloatArray(7)) else (statusParams ?: FloatArray(7))
            val nativeConv    = if (restored) snapshot { SaFBridge.nativeSaFIsConverged() } ?: false else false
            // Resolución de divergencia: iteración más alta gana. En empate,
            // el binario sellado (prefs) tiene prioridad sobre el TXT nativo.
            val usePrefs = prefsSnap.timestampMs > 0 && prefsSnap.iteration >= nativeIter
            if (usePrefs) {
                // Import the checksum-validated snapshot through the native validator;
                // only readback from the optimizer may be displayed as active state.
                val pending = File(context.filesDir, "saf/restore-prefs.txt")
                runCatching {
                    pending.parentFile?.mkdirs()
                    pending.writeText("IVANNA_SAF_STATE_V1\n${prefsSnap.iteration}\n" +
                        prefsSnap.q.joinToString(" ") + "\n")
                    if (SaFBridge.nativeSaFLoadState(pending.absolutePath)) {
                        SaFBridge.nativeSaFSaveState(statePath())
                    }
                }.onFailure { android.util.Log.w(TAG, "Restauración SAF rechazada", it) }
                pending.delete()
            }
            val iter = snapshot { SaFBridge.nativeSaFGetIteration() } ?: nativeIter
            val params = snapshot { SaFBridge.nativeSaFGetParams() } ?: nativeParams
            val conv = (snapshot { SaFBridge.nativeSaFIsConverged() } ?: nativeConv) && iter > 0
            _state.value = SaFState(
                jniLoaded   = loaded,
                modelLoaded = modelOk,
                iteration   = iter,
                params      = params,
                deliveryMessage = publishProfile(params),
                converged   = conv,
                phase       = if (conv) SaFPhase.DONE else SaFPhase.IDLE
            )
            publishLatestState()
        } else {
            _state.value = SaFState(jniLoaded = loaded)
            publishLatestState()
        }
    }

    // ── Start / reset calibration session ────────────────────────────────
    fun startCalibration() {
        if (!_state.value.modelLoaded || !IvannaNativeLib.isLoaded ||
            !feedbackPending.compareAndSet(false, true)) {
            _state.update { it.copy(message = "Espera a que el modelo SAF esté disponible.") }
            return
        }
        stimulusGeneration.incrementAndGet()
        player.stop()
        scope.launch {
            try {
                SaFBridge.nativeSaFReset()
                check(SaFBridge.nativeSaFGetIteration() == 0)
                val params = SaFBridge.nativeSaFGetParams() ?: error("Sin lectura SAF")
                check(params.size == 7 && params.all { it.isFinite() })
                SaFBridge.nativeSaFSaveState(statePath())
                SaFCalibrationPrefs.clear(context)
                _state.value = SaFState(
                    phase = SaFPhase.CALIBRATING, currentDir = SaFDirection.FRONT,
                    params = params, jniLoaded = true, modelLoaded = _state.value.modelLoaded,
                    deliveryMessage = publishProfile(params)
                )
                publishLatestState()
                playStimulus(SaFDirection.FRONT)
            } catch (e: Exception) {
                _state.update { it.copy(phase = SaFPhase.IDLE, stimulusReady = false,
                    message = "No se confirmó el reinicio del motor SAF.") }
                publishLatestState()
            } finally { feedbackPending.set(false) }
        }
    }

    /** Existing bridge publishes to the local control bus and requests daemon delivery.
     * Its Boolean is transport acknowledgement, never proof that audio used the profile. */
    private fun publishProfile(params: FloatArray): String {
        val prefs = com.ivanna.omega.ui.SpatialAudioPrefs.load(context)
        val route = com.ivanna.omega.audio.AudioRouteManager.detectOutputRoute()
        if (!prefs.safEnabled || !com.ivanna.omega.audio.ExternalStereoPolicy.allowHeadphoneHrtf(
                route, prefs.externalStereoLineOut, prefs.hrtfEnabled)) return "Perfil local; envío binaural global deshabilitado para esta configuración."
        val accepted = runCatching {
            com.ivanna.omega.magisk.OmegaEngineBridge.pushSafLatentQ(params, prefs.safIntensity)
        }.getOrDefault(false)
        return if (accepted) "Perfil enviado al control global; aplicación acústica pendiente de comprobar."
            else "Perfil local actualizado; el daemon no confirmó la entrega global."
    }

    /** Replay does not count as feedback; only a completed track unlocks answers. */
    fun replayStimulus(direction: SaFDirection) {
        if (_state.value.phase == SaFPhase.CALIBRATING && !feedbackPending.get() &&
            direction == _state.value.currentDir) playStimulus(direction)
    }

    private fun playStimulus(direction: SaFDirection) {
        val generation = stimulusGeneration.incrementAndGet()
        _state.update { it.copy(stimulusReady = false, message = "Reproduciendo estímulo…") }
        playerScope.launch {
            if (generation != stimulusGeneration.get()) return@launch
            val started = runCatching {
                player.play(direction) { completed ->
                    if (generation == stimulusGeneration.get()) {
                        _state.update { it.copy(stimulusReady = completed, message =
                            if (completed) "Estímulo completado; responde solo si lo escuchaste por auriculares."
                            else "Reproducción incompleta. Repite el tono antes de responder.") }
                        publishLatestState()
                    }
                }
            }.getOrDefault(false)
            if (!started && generation == stimulusGeneration.get()) {
                _state.update { it.copy(stimulusReady = false, message = "No se pudo reproducir. Comprueba la salida y repite.") }
                publishLatestState()
            }
        }
    }

    // ── Feed one feedback sample ──────────────────────────────────────────
    fun feedFeedback(direction: SaFDirection, correct: Boolean) {
        val current = _state.value
        if (current.phase != SaFPhase.CALIBRATING || direction != current.currentDir ||
            !current.stimulusReady || !current.modelLoaded || !feedbackPending.compareAndSet(false, true)) return
        _state.update { it.copy(stimulusReady = false) }
        scope.launch {
          try {
            val applied = runCatching {
                check(IvannaNativeLib.isLoaded)
                SaFBridge.nativeSaFFeedback(direction.ordinal, correct)
                check(SaFBridge.nativeSaFGetIteration() == current.iteration + 1)
            }.isSuccess
            if (!applied) {
                _state.update { it.copy(message = "SAF no confirmó el paso. Reinicia la calibración.") }
                return@launch
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

            val safStatus = snapshot { SaFBridge.nativeSaFGetStatus() }
            val iter     = snapshot { SaFBridge.nativeSaFGetIteration() } ?: 0
            val params   = snapshot { SaFBridge.nativeSaFGetParams() }
                ?: (if (safStatus != null && safStatus.size >= 7) safStatus.copyOfRange(0, 7) else FloatArray(7))
            val modelOk  = safStatus != null && safStatus.size >= 8 && safStatus[7] >= 0.5f
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
                jniLoaded   = IvannaNativeLib.isLoaded,
                modelLoaded = modelOk,
                deliveryMessage = publishProfile(params)
            )
            publishLatestState()

            // FIX (tonos): emitir el estímulo de la siguiente dirección al
            // instante de avançar. Si ya convergió (phase=DONE), silencio.
            // playStimulus() es asíncrono en su propio scope — el motor
            // no espera al hardware.
            if (phase == SaFPhase.CALIBRATING) {
                playStimulus(nextDir)
            }
          } finally { feedbackPending.set(false) }
        }
    }

    /**
     * Libera recursos del motor. Llamar desde el componente Compose que
     * posea el engine (DisposableEffect). Idempotente y tolerante a fallo
     * parcial — cada cancelación va en runCatching para que una falla en el
     * JNI no impida liberar el player.
     */
    fun release() {
        stimulusGeneration.incrementAndGet()
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
