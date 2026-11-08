package com.ivanna.omega.neuromorphic

import android.os.SystemClock
import android.util.Log
import com.ivanna.omega.core.IvannaNativeLib

/**
 * NeuroSensorGateway — puerta de entrada de los frames de sensor reales
 * al bucle cerrado neuro-acústico (v2.5.365).
 *
 * NO genera datos. NO hay sensor simulado: si ningún driver de sensor
 * real está registrado, [submitFrame] devuelve false y el bucle nativo
 * permanece apagado (fail-closed). Un driver es el SDK del hardware
 * (auricular con EEG, equipo ABR o fNIRS) y es quien empuja los frames;
 * este gateway solo valida y reenvía a JNI.
 *
 * Reloj: los timestamps son CLOCK_MONOTONIC en microsegundos, el mismo
 * dominio que usa el controlador nativo (ver monotonicNowUs()). En
 * Kotlin eso es [SystemClock.uptimeMillis] * 1000 — NO elapsedRealtime
 * (ese es CLOCK_BOOTTIME y difiere tras suspender).
 *
 * Consentimiento: el bucle solo se activa con [enable], que debe
 * llamarse desde una acción explícita del usuario (no al arrancar la
 * app). La firma auditiva vive solo en RAM del proceso y no se persiste.
 */
object NeuroSensorGateway {
    private const val TAG = "NeuroSensorGateway"

    enum class Kind(val id: Int) {
        EEG_IN_EAR(2),
        ABR(3),
        FNIRS(4)
    }

    /** Driver de un sensor real. Lo implementa el SDK del hardware. */
    interface Driver {
        val kind: Kind
        fun start()
        fun stop()
    }

    @Volatile
    private var driver: Driver? = null

    @Volatile
    var isEnabled: Boolean = false
        private set

    /** Registra el driver del hardware. No activa el bucle por sí solo. */
    fun registerDriver(driver: Driver) {
        this.driver = driver
        Log.i(TAG, "Driver registrado: ${driver.kind}")
    }

    /** Activa el bucle (acción explícita del usuario) y arranca el driver. */
    fun enable(): Boolean {
        val d = driver
        if (d == null) {
            Log.w(TAG, "enable: sin driver de sensor registrado; el bucle sigue apagado.")
            return false
        }
        return IvannaNativeLib.guardedNative(false) {
            IvannaNativeLib.nativeSetNeuroLoopEnabled(true)
            d.start()
            isEnabled = true
            Log.i(TAG, "Bucle cerrado activado con ${d.kind}")
            true
        }
    }

    /** Apaga el bucle y detiene el driver. La firma aprendida se descarta. */
    fun disable() {
        IvannaNativeLib.guardedNative(Unit) {
            IvannaNativeLib.nativeSetNeuroLoopEnabled(false)
        }
        driver?.stop()
        isEnabled = false
    }

    /**
     * Reenvía UN frame real del driver al motor nativo. Valida antes de
     * cruzar JNI; el nativo vuelve a validar (defensa en profundidad).
     * [timestampUs]: uptimeMillis()*1000 en el momento de la medición.
     * [response]/[coherence]: exactamente 8 valores en [0..1], bandas
     * Greenwood (120 Hz .. 16 kHz). Devuelve true solo si el frame se
     * aceptó y se aprendió.
     */
    fun submitFrame(
        kind: Kind,
        timestampUs: Long,
        response: FloatArray,
        coherence: FloatArray,
        quality: Float,
        abrWaveVLatencyMs: Float = 0f,
        fnirsHbO: Float = 0f
    ): Boolean {
        if (!isEnabled) return false
        if (response.size != 8 || coherence.size != 8) return false
        if (timestampUs <= 0L) return false
        if (!quality.isFinite() || quality < 0f || quality > 1f) return false
        if (response.any { !it.isFinite() || it < 0f || it > 1f }) return false
        if (coherence.any { !it.isFinite() || it < 0f || it > 1f }) return false
        return IvannaNativeLib.guardedNative(false) {
            IvannaNativeLib.nativeNeuroLoopIngest(
                kind.id, timestampUs, response, coherence, quality,
                abrWaveVLatencyMs, fnirsHbO
            )
        }
    }

    /** Reloj correcto para [submitFrame] en el momento de la medición. */
    fun nowUs(): Long = SystemClock.uptimeMillis() * 1000L

    /** [0] active 0/1, [1] confidence, [2] framesLearned, [3] wet. */
    fun status(): FloatArray = IvannaNativeLib.guardedNative(
        floatArrayOf(0f, 0f, 0f, 0f)
    ) {
        IvannaNativeLib.nativeNeuroLoopStatus()
    }
}
