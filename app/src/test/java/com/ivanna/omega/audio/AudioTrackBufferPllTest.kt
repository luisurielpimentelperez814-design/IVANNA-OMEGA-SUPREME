package com.ivanna.omega.audio

import kotlin.math.abs
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class AudioTrackBufferPllTest {
    @Test fun producerAheadReducesOutputRateWithBoundedSlew() {
        val pll = AudioTrackBufferPll(48_000)
        var timeNs = 1_000_000_000L
        assertEquals(0.0, pll.observe(8_000, 0, 10_000, timeNs), 0.0)
        repeat(8) {
            timeNs += 100_000_000L
            pll.observe(8_000, 0, 10_000, timeNs)
        }
        assertTrue("full queue must reduce output frame production", pll.correctionPpm < 0.0)
        assertTrue("correction must remain bounded", pll.correctionPpm in -500.0..500.0)
        assertTrue("integrator must not cause a sudden correction", pll.correctionPpm >= -200.0)
        assertTrue(pll.isLocked)
        assertEquals(8_000L, pll.occupancyFrames)
        assertEquals(5_000L, pll.targetOccupancyFrames)
    }

    @Test fun consumerAheadIncreasesOutputRate() {
        val pll = AudioTrackBufferPll(48_000)
        var timeNs = 1_000_000_000L
        pll.observe(2_000, 0, 10_000, timeNs)
        repeat(5) {
            timeNs += 100_000_000L
            pll.observe(2_000, 0, 10_000, timeNs)
        }
        assertTrue("low queue must increase output frame production", pll.correctionPpm > 0.0)
        assertTrue(pll.correctionPpm <= 125.0)
    }

    @Test fun smallBufferErrorInsideDeadbandDoesNotHunt() {
        val pll = AudioTrackBufferPll(48_000)
        var timeNs = 1_000_000_000L
        // Capacity 10,000; target 5,000; 5,100 is only 1% above target.
        var submitted = 5_100L
        var consumed = 0L
        pll.observe(submitted, consumed, 10_000, timeNs)
        repeat(20) {
            timeNs += 100_000_000L
            submitted += 4_800L
            consumed += 4_800L
            val correction = pll.observe(submitted, consumed, 10_000, timeNs)
            assertEquals("deadband suppresses small occupancy noise", 0.0, correction, 0.0)
        }
    }

    @Test fun sustainedSaturationRemainsBounded() {
        val pll = AudioTrackBufferPll(48_000)
        var timeNs = 1_000_000_000L
        pll.observe(9_000, 0, 10_000, timeNs)
        repeat(300) {
            timeNs += 100_000_000L
            assertTrue(pll.observe(9_000, 0, 10_000, timeNs) in -500.0..500.0)
        }
        // Cola 9000/10000 (error 0.38 tras deadband): demanda = -(800*0.38 +
        // 120*integral). Con el riel en ±1000 (primer intento de la 415) el
        // integrador llegaba a su tope de 4 s y la demanda era -784 ppm.
        // Con el riel del contrato (±500) NO: el anti-windup de integración
        // condicional congela el integrador en cuanto la demanda cruza el
        // riel (es el comportamiento correcto: no se acumula más allá de la
        // autoridad), así que la demanda queda congelada apenas pasada de
        // -500. Lo que el test exige es el contrato: corrección clavada en
        // -500, demanda por encima de la autoridad y riel reportado.
        assertEquals(-500.0, pll.correctionPpm, 0.0)
        assertTrue("la demanda supera la autoridad de ±500", pll.demandedPpm <= -500.0)
        assertTrue("la demanda supera la autoridad de ±500: está en el riel", pll.isAtRail)
    }

    @Test fun invalidQueueAndTimestampFailClosed() {
        val pll = AudioTrackBufferPll(48_000)
        pll.observe(8_000, 0, 10_000, 1_000_000_000L)
        pll.observe(8_000, 0, 10_000, 1_100_000_000L)
        assertTrue(pll.correctionPpm < 0.0)

        assertEquals(0.0, pll.observe(10, 20, 10_000, 1_200_000_000L), 0.0)
        assertEquals(0.0, pll.correctionPpm, 0.0)
        assertTrue(!pll.isLocked)
    }

    @Test fun rollingRegressionMeasuresPositiveConsumerClockDriftAndFeedsForward() {
        val pll = AudioTrackBufferPll(48_000)
        var timeNs = 1_000_000_000L
        var consumed = 0L
        repeat(20) {
            // 48,048 frames per second = +1,000 ppm against 48 kHz nominal.
            val submitted = consumed + 5_000L
            pll.observe(submitted, consumed, 10_000, timeNs)
            consumed += 12_012L
            timeNs += 250_000_000L
        }
        assertTrue("regression must lock after paired frame/time samples", pll.isDriftLocked)
        assertEquals(48_048.0, pll.measuredRateHz, 0.5)
        assertEquals(1_000.0, pll.measuredDriftPpm, 10.0)
        assertTrue("faster consumer needs positive resampler production correction", pll.correctionPpm > 0.0)
        assertTrue(pll.correctionPpm <= 500.0)
    }

    @Test fun negativeConsumerClockDriftProducesNegativeResamplerCorrection() {
        val pll = AudioTrackBufferPll(48_000)
        var timeNs = 1_000_000_000L
        var consumed = 0L
        repeat(20) {
            // 47,952 frames/s = -1,000 ppm against 48 kHz nominal.
            val submitted = consumed + 5_000L
            pll.observe(submitted, consumed, 10_000, timeNs)
            consumed += 11_988L
            timeNs += 250_000_000L
        }
        assertTrue("regression must lock on a slower consumer clock", pll.isDriftLocked)
        assertEquals(47_952.0, pll.measuredRateHz, 0.5)
        assertEquals(-1_000.0, pll.measuredDriftPpm, 10.0)
        assertTrue("slower consumer must request negative resampler correction", pll.correctionPpm < 0.0)
        assertTrue(pll.correctionPpm >= -500.0)
    }

    @Test fun largeNegativeDriftRailsAtFiveHundredPpmAndReportsDemand415() {
        // Bandera 1 de la v2.5.414: con deriva de -1500 ppm el servo se
        // clava en -500 (riel del contrato SAFMUS) sin esconderlo: la
        // demanda sin clampar (-1500) queda visible en demandedPpm/isAtRail
        // para la telemetría. La 415 primero ensanchó el riel a ±1000 y la
        // CI lo rechazó — el riel no era la causa, era la contabilidad.
        val pll = AudioTrackBufferPll(48_000)
        var consumed = 0L
        repeat(100) { k ->
            val t = 1.0 + k * 0.25
            consumed = (t * 48_000.0 * (1.0 - 1500.0e-6)).toLong()
            pll.observe(consumed + 5_000L, consumed, 10_000, (t * 1.0e9).toLong())
        }
        assertTrue(pll.isDriftLocked)
        assertEquals(-1500.0, pll.measuredDriftPpm, 10.0)
        assertEquals(-1500.0, pll.demandedPpm, 20.0)
        assertEquals(-500.0, pll.correctionPpm, 0.0)
        assertTrue("la demanda supera la autoridad: el servo está en el riel", pll.isAtRail)
    }

    @Test fun driftWithinRailIsTrackedWithoutRailing415() {
        // Deriva de -400 ppm (dentro del riel de ±500, muy por encima de
        // cualquier deriva real medida: -6 a -70 ppm): el servo la sigue
        // exacta, sin clavarse. Es el caso simétrico al del riel: dentro
        // de la autoridad no hay saturación; fuera, se reporta con isAtRail.
        val pll = AudioTrackBufferPll(48_000)
        var consumed = 0L
        repeat(100) { k ->
            val t = 1.0 + k * 0.25
            consumed = (t * 48_000.0 * (1.0 - 400.0e-6)).toLong()
            pll.observe(consumed + 5_000L, consumed, 10_000, (t * 1.0e9).toLong())
        }
        assertTrue(pll.isDriftLocked)
        assertEquals(-400.0, pll.measuredDriftPpm, 10.0)
        assertEquals(-400.0, pll.demandedPpm, 20.0)
        assertEquals(-400.0, pll.correctionPpm, 20.0)
        assertTrue("deriva dentro de la autoridad: no debe estar en el riel", !pll.isAtRail)
    }

    @Test fun implausibleClockOutlierClearsStaleFeedForwardAndReacquires() {
        val pll = AudioTrackBufferPll(48_000)
        var timeNs = 1_000_000_000L
        var consumed = 0L
        repeat(8) {
            pll.observe(consumed + 5_000L, consumed, 10_000, timeNs)
            consumed += 12_012L
            timeNs += 250_000_000L
        }
        assertTrue("valid clock samples should establish drift lock", pll.isDriftLocked)
        assertEquals(1_000.0, pll.measuredDriftPpm, 10.0)

        // A single impossible frame/timestamp pair must not remain in the OLS
        // window or keep the previous clock feed-forward alive.
        consumed += 20_000L
        timeNs += 250_000_000L
        pll.observe(consumed + 5_000L, consumed, 10_000, timeNs)

        assertTrue("outlier must drop clock lock until fresh samples arrive", !pll.isDriftLocked)
        assertEquals(0.0, pll.measuredDriftPpm, 0.0)
        assertEquals(0.0, pll.measuredRateHz, 0.0)
    }

    @Test fun frozenPresentedFramePositionClearsDriftEstimate() {
        val pll = AudioTrackBufferPll(48_000)
        var timeNs = 1_000_000_000L
        var consumed = 0L
        repeat(8) {
            pll.observe(consumed + 5_000L, consumed, 10_000, timeNs)
            consumed += 12_012L
            timeNs += 250_000_000L
        }
        assertTrue(pll.isDriftLocked)
        assertEquals(1_000.0, pll.measuredDriftPpm, 10.0)
        consumed -= 12_012L // reuse the last presented frame position
        timeNs += 250_000_000L
        val correction = pll.observe(consumed + 5_000L, consumed, 10_000, timeNs)
        assertTrue(!pll.isDriftLocked)
        assertEquals(0.0, pll.measuredDriftPpm, 0.0)
        assertEquals(0.0, pll.measuredRateHz, 0.0)
        assertTrue(correction in -500.0..500.0)
    }

    @Test fun timestampRegressionResetsClockAndActuatorToUnity() {
        val pll = AudioTrackBufferPll(48_000)
        pll.observe(5_000, 0, 10_000, 2_000_000_000L)
        repeat(6) { i ->
            pll.observe(5_000 + (i + 1) * 12_000L, (i + 1) * 12_000L, 10_000,
                2_000_000_000L + (i + 1) * 250_000_000L)
        }
        assertEquals(0.0, pll.observe(1_000, 100, 10_000, 1_000_000_000L), 0.0)
        assertEquals(0.0, pll.correctionPpm, 0.0)
        assertTrue(!pll.isLocked)
    }

    @Test fun outputCorrectionAlwaysStaysWithinFiveHundredPpm() {
        val pll = AudioTrackBufferPll(48_000)
        var timeNs = 1_000_000_000L
        pll.observe(9_000, 0, 10_000, timeNs)
        repeat(200) {
            timeNs += 100_000_000L
            val value = pll.observe(9_000, 0, 10_000, timeNs)
            assertTrue(value in -500.0..500.0)
        }
        // Misma demanda que sustainedSaturationRemainsBounded: -784 ppm,
        // que supera la autoridad de ±500: la corrección se clava en -500.
        assertEquals(-500.0, pll.correctionPpm, 0.0)
    }

    // ── Jitter de timestamp REAL de la salida USB (AudioFlinger, captura Moto G85 + DAC USB full-speed) ──
    // sigma ~0.137 ms; picos de hasta 109 ms (dump: jitterMs std=0.137 max=109). Generador determinista.
    private class UsbTimestampModel(private val trueDriftPpm: Double, seed: Long) {
        private var state = seed
        private fun u(): Double { state = state * 6364136223846793005L + 1442695040888963407L
            return ((state ushr 11).toDouble() / (1L shl 53).toDouble()) }
        private fun gauss(): Double { var a = 0.0; repeat(12) { a += u() }; return a - 6.0 }
        fun jitterNs(): Long {
            var j = gauss() * 0.137e-3
            if (u() < 5.0e-4) j += 0.002 + u() * 0.107        // pico atipico positivo (2..109 ms)
            return (j * 1.0e9).toLong()
        }
        fun framesAt(tSeconds: Double): Long = (tSeconds * 48_000.0 * (1.0 + trueDriftPpm * 1.0e-6)).toLong()
    }

    private fun runUsbSession(trueDriftPpm: Double, polls: Int, seed: Long): Pair<DoubleArray, DoubleArray> {
        val pll = AudioTrackBufferPll(48_000)
        val model = UsbTimestampModel(trueDriftPpm, seed)
        val measured = DoubleArray(polls); val corr = DoubleArray(polls)
        for (k in 0 until polls) {
            val t = 1.0 + k * 0.25
            val consumed = model.framesAt(t)
            pll.observe(consumed + 5_000L, consumed, 10_000, (t * 1.0e9).toLong() + model.jitterNs())
            measured[k] = pll.measuredDriftPpm; corr[k] = pll.correctionPpm
        }
        return measured to corr
    }

    @Test fun usbTimestampJitterAndSpikesDoNotCorruptDriftEstimate() {
        for ((trueDrift, seed) in listOf(10.0 to 1L, -18.0 to 2L, 0.0 to 3L)) {
            val (measured, corr) = runUsbSession(trueDrift, polls = 480, seed = seed)   // 120 s
            val tail = measured.copyOfRange(380, 480)
            val worst = tail.maxOf { abs(it - trueDrift) }
            assertTrue("deriva real $trueDrift ppm: error maximo de la estimacion $worst ppm (limite 12 ppm)", worst < 12.0)
            val corrWorst = corr.copyOfRange(380, 480).maxOf { abs(it - trueDrift) }
            assertTrue("la correccion debe seguir la deriva real $trueDrift ppm (error $corrWorst ppm, limite 15 ppm)", corrWorst < 15.0)
        }
    }

    @Test fun naiveEightPointOlsWouldFailTheSameJitterNegativeControl() {
        // Caso negativo: el estimador ANTERIOR (OLS de 8 puntos) sobre exactamente los mismos datos debe superar
        // el limite que el estimador actual cumple; demuestra que la prueba anterior detecta el defecto.
        val model = UsbTimestampModel(10.0, 1L)
        val tNs = LongArray(480); val f = LongArray(480)
        for (k in 0 until 480) { val t = 1.0 + k * 0.25; f[k] = model.framesAt(t); tNs[k] = (t * 1.0e9).toLong() + model.jitterNs() }
        var worst = 0.0
        for (end in 388..480) {
            val n = 8; var mt = 0.0; var mf = 0.0
            for (i in end - n until end) { mt += (tNs[i] - tNs[end - n]) * 1e-9; mf += (f[i] - f[end - n]).toDouble() }
            mt /= n; mf /= n
            var cov = 0.0; var v = 0.0
            for (i in end - n until end) { val a = (tNs[i] - tNs[end - n]) * 1e-9 - mt; val b = (f[i] - f[end - n]).toDouble() - mf; cov += a * b; v += a * a }
            worst = maxOf(worst, abs((cov / v / 48_000.0 - 1.0) * 1.0e6 - 10.0))
        }
        assertTrue("el OLS de 8 puntos debia fallar el limite de 12 ppm (peor error $worst ppm)", worst > 12.0)
    }

    @Test fun singleHugeTimestampSpikeDoesNotMoveTheEstimate() {
        val pll = AudioTrackBufferPll(48_000)
        for (k in 0 until 200) {
            val t = 1.0 + k * 0.25
            val consumed = (t * 48_000.0 * (1.0 + 10.0e-6)).toLong()
            val spike = if (k == 150) 109_000_000L else 0L
            pll.observe(consumed + 5_000L, consumed, 10_000, (t * 1.0e9).toLong() + spike)
        }
        assertEquals(10.0, pll.measuredDriftPpm, 3.0)
    }

    // ── v2.5.419: INANICIÓN NO ES DERIVA DE RELOJ ──
    // Verificado en vivo en la 417/418: con el DSP al límite (dsp_us ~7 ms
    // por bloque > 6.67 ms de presupuesto) el track se queda sin audio,
    // framePosition avanza lento y el servo lo leía como reloj lento:
    // deriva medida de -7 a -577 ppm en 10 s, demanda hasta -1040 ppm, ppm
    // clavado en -500. La pendiente de un intervalo con underruns NO es
    // una medida de reloj: el servo debe descartarla, no corregirla.
    @Test fun starvationUnderruns_doNotCreateFalseClockDrift419() {
        val pll = AudioTrackBufferPll(48_000)
        repeat(100) { k ->
            val t = 1.0 + k * 0.25
            val slow = k >= 20
            // Fase 1 (k < 20): reloj nominal, estima bloqueada en ~0.
            // Fase 2 (k >= 20): el track avanza lento (-600 ppm, como la
            // deriva medida en vivo) PERO con underruns subiendo en cada
            // sondeo: es inanición, no un reloj lento.
            val consumed = if (slow) (t * 48_000.0 * (1.0 - 600.0e-6)).toLong()
                else (t * 48_000.0).toLong()
            val underruns = if (slow) (k - 19).toLong() else 0L
            pll.observe(consumed + 5_000L, consumed, 10_000, (t * 1.0e9).toLong(), underruns = underruns)
        }
        assertTrue("la inanición no debe bloquear una estima de reloj", !pll.isDriftLocked)
        assertEquals(0.0, pll.measuredDriftPpm, 0.0)
        assertEquals(0.0, pll.demandedPpm, 0.0)
        assertEquals(0.0, pll.correctionPpm, 0.0)
        assertTrue("la inanición no debe clavar el servo en el riel", !pll.isAtRail)
    }

    @Test fun sameSlowClockWithoutUnderruns_isMeasuredAndCorrected419() {
        // Control del test anterior: la MISMA pendiente lenta (-600 ppm)
        // SIN underruns es un reloj lento de verdad y el servo SÍ debe
        // medirla y corregirla. Si este test fallara, la puerta de la 419
        // habría cegado al servo.
        val pll = AudioTrackBufferPll(48_000)
        repeat(100) { k ->
            val t = 1.0 + k * 0.25
            val consumed = (t * 48_000.0 * (1.0 - 600.0e-6)).toLong()
            pll.observe(consumed + 5_000L, consumed, 10_000, (t * 1.0e9).toLong(), underruns = 0L)
        }
        assertTrue(pll.isDriftLocked)
        assertEquals(-600.0, pll.measuredDriftPpm, 10.0)
        assertEquals(-600.0, pll.demandedPpm, 20.0)
        assertEquals(-500.0, pll.correctionPpm, 0.0)
        assertTrue(pll.isAtRail)
    }

    @Test fun underrunCountDecrease_isNotTreatedAsStarvation419() {
        // Un contador de underruns que BAJA es un track recreado (o un
        // wraparound), no inanición: no debe disparar la puerta ni cegar
        // la estima de reloj.
        val pll = AudioTrackBufferPll(48_000)
        var timeNs = 1_000_000_000L
        var consumed = 0L
        repeat(20) { k ->
            pll.observe(consumed + 5_000L, consumed, 10_000, timeNs,
                underruns = (5 - k).coerceAtLeast(0).toLong())
            consumed += 12_012L
            timeNs += 250_000_000L
        }
        assertTrue(pll.isDriftLocked)
        assertEquals(1_000.0, pll.measuredDriftPpm, 10.0)
        assertTrue("un reloj rápido real debe pedir corrección positiva", pll.correctionPpm > 0.0)
    }

    // ── v2.5.420: EVOLUCIÓN DEL SERVO — HOLD ANTE SENSOR NO FIABLE ──
    // La 419 descartaba la estima de reloj y el integrador cuando había
    // inanición, pero dejaba que el término P moviera el actuador con una
    // cola distorsionada (el track se vació y se rellena a ráfagas: la cola
    // medida oscila y no es un error real). El controlador debe congelar
    // su salida en la última corrección válida mientras el sensor no sea
    // fiable, y retomar en la próxima medida limpia.
    @Test fun starvationHold_freezesActuatorDespiteQueueError420() {
        val pll = AudioTrackBufferPll(48_000)
        repeat(100) { k ->
            val t = 1.0 + k * 0.25
            val timeNs = (t * 1.0e9).toLong()
            // El reloj presentado es nominal en todo momento (12_000
            // frames/sondeo): el único "error" es la cola.
            val consumed = (t * 48_000.0).toLong()
            // Fase 1 (k < 20): cola en el objetivo (5_000), sin underruns.
            // Fase 2 (k >= 20): la cola salta a 9_000 (error de fase +0.40)
            // CON underruns subiendo en cada sondeo: es inanición, y la cola
            // medida es un artefacto de las ráfagas, no un error real.
            val queue = if (k < 20) 5_000L else 9_000L
            val underruns = if (k < 20) 0L else (k - 19).toLong()
            pll.observe(consumed + queue, consumed, 10_000, timeNs, underruns = underruns)
        }
        assertEquals(0.0, pll.correctionPpm, 0.0)
        assertEquals(0.0, pll.measuredDriftPpm, 0.0)
        assertTrue("el hold no debe bloquear una estima de reloj", !pll.isDriftLocked)
        assertEquals(80L, pll.starvationHolds)
        assertTrue("con hold, el servo no debe clavarse en el riel", !pll.isAtRail)
    }

    @Test fun sameQueueErrorWithoutUnderruns_movesActuator420() {
        // Control del test anterior: el MISMO error de cola (9_000) SIN
        // underruns es un error de cola real y el PI SÍ debe actuar sobre
        // él. Si este test fallara, el hold de la 420 habría dejado al servo
        // sordo ante la cola.
        val pll = AudioTrackBufferPll(48_000)
        repeat(100) { k ->
            val t = 1.0 + k * 0.25
            val timeNs = (t * 1.0e9).toLong()
            val consumed = (t * 48_000.0).toLong()
            val queue = if (k < 20) 5_000L else 9_000L
            pll.observe(consumed + queue, consumed, 10_000, timeNs, underruns = 0L)
        }
        assertTrue("sin inanición, el PI debe corregir el error de cola", pll.correctionPpm <= -400.0)
        assertTrue(pll.isAtRail)
        assertEquals(0L, pll.starvationHolds)
    }

    @Test fun capacityAndTargetAreExposedForTelemetry423() {
        // v2.5.423: la línea de telemetría ya imprime pll_tgt/pll_cap, pero
        // el servo no exponía la capacidad: el objetivo se adivinaba del
        // log (en vivo 2026-11-12, captura: occ ~5600 con ph +0.23 →
        // capacidad ~7670, objetivo ~3835). Ahora ambos quedan registrados.
        val pll = AudioTrackBufferPll(48_000)
        pll.observe(8_000, 0, 10_000, 1_000_000_000L)
        assertEquals(5_000L, pll.targetOccupancyFrames)
        assertEquals(10_000L, pll.lastBufferCapacityFrames)
        pll.reset()
        assertEquals(0L, pll.targetOccupancyFrames)
        assertEquals(0L, pll.lastBufferCapacityFrames)
    }
}
