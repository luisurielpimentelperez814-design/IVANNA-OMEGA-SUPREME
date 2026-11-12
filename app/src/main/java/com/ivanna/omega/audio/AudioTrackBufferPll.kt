package com.ivanna.omega.audio

import kotlin.math.abs

/**
 * SAFMUS clock servo for exactly one app-owned AudioTrack.
 *
 * One controller combines a rolling least-squares estimate of the presented
 * frame clock with a bounded buffer-occupancy PI term. Positive drift means
 * the AudioTrack consumer clock runs faster than nominal, so positive ppm
 * asks StereoAudioResampler to produce output frames faster.
 *
 * This is software rate correction for IVANNA's own track only. It does not
 * discipline a physical DAC, Bluetooth sink, vendor HAL, or another app.
 */
internal class AudioTrackBufferPll(private val nominalSampleRateHz: Int) {
    companion object {
        private const val MIN_INTERVAL_NS = 10_000_000L
        private const val MAX_INTERVAL_NS = 2_000_000_000L
        // Autoridad del servo: ±500 ppm, CONTRATO SAFMUS (lo exige
        // scripts/check_safmus_transversal_wiring.py). La 415 primero lo
        // ensanchó a ±1000 y la CI lo rechazó: el riel NO era la causa
        // del ppm=-500 clavado de la 414 — era el bug de contabilidad
        // (priming/reprimes no contados en captureSubmittedFrames48k),
        // ya corregido. Las derivas reales medidas son -6 a -70 ppm,
        // muy dentro de ±500. Si una deriva real superara el riel, la
        // telemetría (demandedPpm/isAtRail + measuredDriftPpm) lo dirá
        // en vivo; el slew (25 ppm/update) y el anti-windup no cambian.
        private const val MAX_CORRECTION_PPM = 500.0
        private const val MAX_SLEW_PPM_PER_UPDATE = 25.0
        private const val KP_PPM_PER_FULL_BUFFER = 800.0
        private const val KI_PPM_PER_FULL_BUFFER_SECOND = 120.0
        private const val MAX_INTEGRAL_SECONDS = 4.0
        // Suppress small queue-measurement noise; subtract the band for continuity.
        private const val BUFFER_DEADBAND_FRACTION = 0.02
        private const val LOCK_SAMPLES = 5
        // Ventana de 120 muestras a ~250 ms = ~30 s. Con el jitter de timestamp MEDIDO en la salida USB
        // (AudioFlinger: sigma ~0.137 ms, picos de hasta 109 ms) una ventana de 8 puntos (~2 s) da una deriva con
        // desviacion de ~36-54 ppm, mayor que la deriva real (decenas de ppm): el feedforward inyectaba ruido.
        // Con 30 s y estimador robusto baja a ~2 ppm (tools/safmus/usb_drift_estimator_sim.py).
        private const val REGRESSION_WINDOW = 120
        private const val REQUIRED_REGRESSION_POINTS = 4
        private const val MAX_PAIR_POINTS = 40                       // submuestreo: <= 40*39/2 = 780 pendientes
        private const val MAX_PAIR_SLOPES = MAX_PAIR_POINTS * (MAX_PAIR_POINTS - 1) / 2
        private const val FEEDFORWARD_FULL_WEIGHT_SECONDS = 20.0     // el feedforward crece con la ventana
        private const val MAX_MEASURED_DRIFT_PPM = 2_000.0
        private const val DRIFT_FILTER_ALPHA = 0.2
        // Ignore sub-half-ppm estimator noise for actuator feed-forward.
        private const val DRIFT_FEED_FORWARD_DEADBAND_PPM = 0.5
    }

    private var previousTimestampNs = 0L
    private var previousConsumedFrames = -1L
    // v2.5.419: último conteo de underruns visto (AudioTrack.getUnderrunCount).
    // -1 = desconocido (el llamador no lo pasa, o es la primera observación).
    private var previousUnderruns: Long = -1L
    private var integralErrorSeconds = 0.0
    private var validSamples = 0
    private val frameHistory = LongArray(REGRESSION_WINDOW)
    private val timeHistoryNs = LongArray(REGRESSION_WINDOW)
    private var historyCount = 0
    private var historyNext = 0
    private var filteredDriftPpm = 0.0
    private var hasDriftEstimate = false
    private var windowSpanSeconds = 0.0
    private val pairSlopes = DoubleArray(MAX_PAIR_SLOPES)   // preasignado: sin allocs en el hilo del reproductor

    var correctionPpm: Double = 0.0
        private set
    var occupancyFrames: Long = 0L
        private set
    var targetOccupancyFrames: Long = 0L
        private set
    var phaseErrorFraction: Double = 0.0
        private set
    var measuredRateHz: Double = 0.0
        private set
    var measuredDriftPpm: Double = 0.0
        private set
    // Demanda SIN clampar del lazo (feed-forward + PI) en ppm (v2.5.415).
    // Si |demandedPpm| supera MAX_CORRECTION_PPM el servo está en el riel:
    // pide más autoridad de la que tiene (en la 414 la demanda era <= -500
    // con el riel en ±500). Es el dato que faltaba para no descubrir el
    // riel por accidente: la telemetría lo expone como pll_dem/pll_rail.
    var demandedPpm: Double = 0.0
        private set
    // v2.5.420: cuántas veces el servo CONGELÓ su salida por inanición
    // (hold). Contador acumulativo de la sesión, visible en telemetría
    // como pll_hold; se reinicia con reset().
    var starvationHolds: Long = 0L
        private set

    val isLocked: Boolean get() = validSamples >= LOCK_SAMPLES
    val isDriftLocked: Boolean get() = historyCount >= REQUIRED_REGRESSION_POINTS && hasDriftEstimate
    val isAtRail: Boolean get() = abs(demandedPpm) >= MAX_CORRECTION_PPM - 1e-9

    /**
     * @param submittedFrames cumulative stereo frames accepted by this AudioTrack
     * @param consumedFrames cumulative presented frames from this same track's AudioTimestamp
     * @param bufferCapacityFrames configured AudioTrack buffer size in frames
     * @param timestampNs monotonic timestamp paired with consumedFrames
     * @param underruns cumulative AudioTrack.getUnderrunCount() (-1 = desconocido).
     *        Un underrun nuevo invalida la estima de reloj de ese intervalo:
     *        framePosition deja de avanzar (o avanza lento) mientras el reloj
     *        de pared sigue, y esa pendiente mide inanición, no reloj
     *        (v2.5.419: la 417/418 se clavaba en -500 ppm con deriva medida
     *        de -7 a -577 ppm en 10 s por leer inanición como reloj lento).
     * @return bounded ppm correction for StereoAudioResampler
     */
    fun observe(
        submittedFrames: Long,
        consumedFrames: Long,
        bufferCapacityFrames: Int,
        timestampNs: Long,
        underruns: Long = -1L
    ): Double {
        if (nominalSampleRateHz !in 8_000..768_000 ||
            submittedFrames < 0L || consumedFrames < 0L ||
            bufferCapacityFrames <= 0 || timestampNs <= 0L) {
            reset()
            return 0.0
        }

        val queued = submittedFrames - consumedFrames
        val capacity = bufferCapacityFrames.toLong()
        if (queued < 0L || queued > capacity * 2L) {
            reset()
            return 0.0
        }

        // v2.5.419 — INANICIÓN NO ES DERIVA DE RELOJ. Cuando el AudioTrack
        // se queda sin audio (underrun), framePosition deja de avanzar (o
        // avanza lento) mientras el reloj de pared sigue: la pendiente
        // frame/tiempo de ese intervalo mide la inanición, no el reloj. El
        // servo la leía como reloj lento y pedía producir MENOS frames
        // (-500 ppm), lo que agravaba la inanición: realimentación positiva
        // hasta el riel (en vivo, 417/418: deriva medida -7 -> -577 ppm en
        // 10 s, demanda hasta -1040 ppm). Si el contador de underruns subió
        // desde la última observación, la estima de reloj y el integrador
        // (ambos alimentados por una cola distorsionada por el stall) se
        // descartan; el PI proporcional sigue controlando la cola con la
        // medida viva. Sin underruns nuevos, el servo mide el reloj real
        // como siempre: un reloj lento de verdad SÍ se corrige (ver el test
        // de control en AudioTrackBufferPllTest).
        if (underruns >= 0L) {
            val starved = previousUnderruns >= 0L && underruns > previousUnderruns
            previousUnderruns = underruns
            if (starved) {
                clearClockEstimate()
                integralErrorSeconds = 0.0
                starvationHolds++
                // v2.5.420 — HOLD: con el sensor (framePosition) corrompido
                // por la inanición, la cola medida también está
                // distorsionada (el track se vació y se rellena a ráfagas):
                // el término P actuaría sobre un error de cola falso. El
                // controlador congela su salida en la última corrección
                // válida y retoma en la próxima medida limpia. Es el
                // comportamiento fail-closed estándar ante un sensor no
                // fiable: no se actúa sobre una medida que no se cree.
                // La 419 descartaba la estima de reloj y el integrador,
                // pero dejaba que el P moviera el actuador con la cola
                // distorsionada; el hold cierra ese hueco.
                return correctionPpm
            }
        }

        occupancyFrames = queued
        targetOccupancyFrames = (capacity / 2L).coerceAtLeast(1L)
        phaseErrorFraction = ((queued - targetOccupancyFrames).toDouble() / capacity)
            .coerceIn(-1.0, 1.0)

        if (previousTimestampNs == 0L) {
            previousTimestampNs = timestampNs
            previousConsumedFrames = consumedFrames
            addClockSample(consumedFrames, timestampNs)
            return 0.0
        }

        if (timestampNs <= previousTimestampNs || consumedFrames < previousConsumedFrames) {
            reset()
            previousTimestampNs = timestampNs
            previousConsumedFrames = consumedFrames
            addClockSample(consumedFrames, timestampNs)
            return 0.0
        }

        val elapsedNs = timestampNs - previousTimestampNs
        if (elapsedNs < MIN_INTERVAL_NS) return correctionPpm
        if (elapsedNs > MAX_INTERVAL_NS) {
            reset()
            previousTimestampNs = timestampNs
            previousConsumedFrames = consumedFrames
            addClockSample(consumedFrames, timestampNs)
            return 0.0
        }

        // A timestamp whose frame position stops advancing is not a valid
        // clock-rate observation. Drop stale drift feed-forward immediately;
        // keep the independent buffer PI alive so queue control can still recover.
        if (consumedFrames == previousConsumedFrames) {
            resetClockEstimateAt(consumedFrames, timestampNs)
        } else {
            // Compuerta de plausibilidad POR MUESTRA: el ritmo instantaneo entre dos observaciones consecutivas debe
            // ser fisicamente creible (0.7x..1.3x nominal; una deriva real es ~0.1 %). Un salto mayor es una discontinuidad del HAL/ruta, no una
            // pendiente de reloj: se descarta la ventana y se readquiere. La mediana de Theil-Sen ya soporta atipicos
            // pequenos; esta compuerta cubre los saltos que desplazarian toda la ventana.
            val instantRate = (consumedFrames - previousConsumedFrames) * 1.0e9 / elapsedNs.toDouble()
            if (instantRate < nominalSampleRateHz * 0.7 || instantRate > nominalSampleRateHz * 1.3) {
                resetClockEstimateAt(consumedFrames, timestampNs)
            }
        }

        val dtSeconds = elapsedNs / 1_000_000_000.0
        val occupancyError = when {
            abs(phaseErrorFraction) <= BUFFER_DEADBAND_FRACTION -> 0.0
            phaseErrorFraction > 0.0 -> phaseErrorFraction - BUFFER_DEADBAND_FRACTION
            else -> phaseErrorFraction + BUFFER_DEADBAND_FRACTION
        }
        val candidateIntegral = (integralErrorSeconds + occupancyError * dtSeconds)
            .coerceIn(-MAX_INTEGRAL_SECONDS, MAX_INTEGRAL_SECONDS)

        previousTimestampNs = timestampNs
        previousConsumedFrames = consumedFrames
        addClockSample(consumedFrames, timestampNs)
        estimateClockDrift()

        // Drift feed-forward and buffer PI share one bounded actuator.
        // El peso crece con la duracion de la ventana: con pocos segundos de baseline la pendiente es ruidosa
        // (error ~ jitter/baseline), asi que no se confia plenamente en ella hasta ~20 s.
        val feedForwardWeight = (windowSpanSeconds / FEEDFORWARD_FULL_WEIGHT_SECONDS).coerceIn(0.0, 1.0)
        val driftFeedForward = if (
            isDriftLocked && abs(filteredDriftPpm) >= DRIFT_FEED_FORWARD_DEADBAND_PPM
        ) filteredDriftPpm * feedForwardWeight else 0.0
        val currentRawTarget = driftFeedForward -
            (KP_PPM_PER_FULL_BUFFER * occupancyError +
                KI_PPM_PER_FULL_BUFFER_SECOND * integralErrorSeconds)
        demandedPpm = currentRawTarget

        // Conditional-integration anti-windup: test the actuator's current
        // unsaturated demand, not the candidate integral. This lets one update
        // reach the hard limit exactly; subsequent updates cannot wind farther
        // into saturation. The final actuator remains clamped to ±500 ppm
        // (contrato SAFMUS; el ASRC nativo clampa más ancho, a ±1000).
        val pushesFurtherIntoSaturation =
            (currentRawTarget >= MAX_CORRECTION_PPM && occupancyError < 0.0) ||
            (currentRawTarget <= -MAX_CORRECTION_PPM && occupancyError > 0.0)
        if (!pushesFurtherIntoSaturation) integralErrorSeconds = candidateIntegral

        val occupancyCorrection = -(KP_PPM_PER_FULL_BUFFER * occupancyError +
            KI_PPM_PER_FULL_BUFFER_SECOND * integralErrorSeconds)
        val target = (driftFeedForward + occupancyCorrection)
            .coerceIn(-MAX_CORRECTION_PPM, MAX_CORRECTION_PPM)

        if (validSamples < LOCK_SAMPLES) validSamples++
        val delta = (target - correctionPpm)
            .coerceIn(-MAX_SLEW_PPM_PER_UPDATE, MAX_SLEW_PPM_PER_UPDATE)
        correctionPpm = (correctionPpm + delta)
            .coerceIn(-MAX_CORRECTION_PPM, MAX_CORRECTION_PPM)
        return correctionPpm
    }

    private fun addClockSample(framePosition: Long, timestampNs: Long) {
        if (historyCount > 0) {
            val previousIndex = if (historyNext == 0) REGRESSION_WINDOW - 1 else historyNext - 1
            if (timestampNs <= timeHistoryNs[previousIndex] ||
                framePosition <= frameHistory[previousIndex]) return
        }
        frameHistory[historyNext] = framePosition
        timeHistoryNs[historyNext] = timestampNs
        historyNext = (historyNext + 1) % REGRESSION_WINDOW
        if (historyCount < REGRESSION_WINDOW) historyCount++
    }

    /**
     * Theil-Sen sobre pares frame/timestamp: mediana de las pendientes de todos los pares de una submuestra
     * (<= 40 puntos) de la ventana. Tolera hasta ~29 % de puntos atipicos (picos de jitter de timestamp,
     * discontinuidades del HAL). El tiempo se centra en la muestra mas antigua para no perder precision.
     */
    private fun estimateClockDrift() {
        if (historyCount < REQUIRED_REGRESSION_POINTS) return
        val oldest = if (historyCount == REGRESSION_WINDOW) historyNext else 0
        val t0 = timeHistoryNs[oldest]
        val f0 = frameHistory[oldest]
        val newestIdx = (oldest + historyCount - 1) % REGRESSION_WINDOW
        windowSpanSeconds = (timeHistoryNs[newestIdx] - t0) * 1.0e-9

        val stride = (historyCount + MAX_PAIR_POINTS - 1) / MAX_PAIR_POINTS
        var m = 0
        var i = 0
        while (i < historyCount) {
            val ia = (oldest + i) % REGRESSION_WINDOW
            val ta = (timeHistoryNs[ia] - t0) * 1.0e-9
            val fa = (frameHistory[ia] - f0).toDouble()
            var j = i + stride
            while (j < historyCount) {
                val ib = (oldest + j) % REGRESSION_WINDOW
                val dt = (timeHistoryNs[ib] - t0) * 1.0e-9 - ta
                if (dt > 1.0e-6 && m < MAX_PAIR_SLOPES) {
                    pairSlopes[m++] = ((frameHistory[ib] - f0).toDouble() - fa) / dt
                }
                j += stride
            }
            i += stride
        }
        if (m == 0) return
        java.util.Arrays.sort(pairSlopes, 0, m)
        val rate = if (m % 2 == 1) pairSlopes[m / 2] else 0.5 * (pairSlopes[m / 2 - 1] + pairSlopes[m / 2])
        val drift = (rate / nominalSampleRateHz.toDouble() - 1.0) * 1_000_000.0
        if (!rate.isFinite() || !drift.isFinite() ||
            rate < nominalSampleRateHz * 0.5 ||
            rate > nominalSampleRateHz * 1.5 ||
            abs(drift) > MAX_MEASURED_DRIFT_PPM) {
            // The rejected point is already in the rolling window. Keeping it
            // would bias every following OLS fit until it ages out, while the
            // previous feed-forward estimate could remain active indefinitely.
            // Fail closed for clock feed-forward and reacquire from the newest
            // monotonic pair; the independent buffer PI may continue to operate.
            resetClockEstimateToLatest()
            return
        }
        measuredRateHz = rate
        measuredDriftPpm = drift
        filteredDriftPpm = if (hasDriftEstimate) {
            filteredDriftPpm + DRIFT_FILTER_ALPHA * (drift - filteredDriftPpm)
        } else {
            drift
        }
        hasDriftEstimate = true
    }

    private fun resetClockEstimateToLatest() {
        val latest = if (historyCount == 0) -1 else
            if (historyNext == 0) REGRESSION_WINDOW - 1 else historyNext - 1
        val latestFrame = if (latest >= 0) frameHistory[latest] else previousConsumedFrames
        val latestTime = if (latest >= 0) timeHistoryNs[latest] else previousTimestampNs
        resetClockEstimateAt(latestFrame, latestTime)
    }

    // Vacía la estima de reloj sin sembrar: la próxima muestra limpia
    // reconstruye la ventana desde cero (v2.5.419, inanición). La muestra
    // actual se añade después por el flujo normal de observe(): es el
    // extremo del intervalo inanido y el siguiente intervalo (limpio) es
    // el que define la pendiente.
    private fun clearClockEstimate() {
        frameHistory.fill(0L)
        timeHistoryNs.fill(0L)
        historyCount = 0
        historyNext = 0
        filteredDriftPpm = 0.0
        hasDriftEstimate = false
        windowSpanSeconds = 0.0
        measuredRateHz = 0.0
        measuredDriftPpm = 0.0
    }

    private fun resetClockEstimateAt(framePosition: Long, timestampNs: Long) {
        clearClockEstimate()
        if (framePosition >= 0L && timestampNs > 0L) {
            frameHistory[0] = framePosition
            timeHistoryNs[0] = timestampNs
            historyCount = 1
            historyNext = 1
        }
    }

    fun reset() {
        previousTimestampNs = 0L
        previousConsumedFrames = -1L
        previousUnderruns = -1L
        starvationHolds = 0L
        integralErrorSeconds = 0.0
        validSamples = 0
        historyCount = 0
        historyNext = 0
        filteredDriftPpm = 0.0
        hasDriftEstimate = false
        windowSpanSeconds = 0.0
        correctionPpm = 0.0
        demandedPpm = 0.0
        occupancyFrames = 0L
        targetOccupancyFrames = 0L
        phaseErrorFraction = 0.0
        measuredRateHz = 0.0
        measuredDriftPpm = 0.0
        frameHistory.fill(0L)
        timeHistoryNs.fill(0L)
    }
}
