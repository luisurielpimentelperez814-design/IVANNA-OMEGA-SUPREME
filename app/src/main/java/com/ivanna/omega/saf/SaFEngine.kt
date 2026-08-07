package com.ivanna.omega.saf

import android.content.Context
import android.content.res.AssetManager
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.spatial.SaFOptimizer
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
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

    // ── Init ─────────────────────────────────────────────────────────────
    fun initialize() {
        scope.launch {
            val jsonPath = resolveJsonPath()
            val loaded   = IvannaNativeLib.isLoaded
            if (loaded) runCatching { SaFBridge.nativeSaFInit(jsonPath) }
            _state.value = SaFState(jniLoaded = loaded)
        }
    }

    // ── Start / reset calibration session ────────────────────────────────
    fun startCalibration() {
        if (IvannaNativeLib.isLoaded) runCatching { SaFBridge.nativeSaFReset() }
        _state.value = SaFState(
            phase      = SaFPhase.CALIBRATING,
            currentDir = SaFDirection.ordered[0],
            jniLoaded  = IvannaNativeLib.isLoaded
        )
    }

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
        }
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
}
