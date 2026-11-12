package com.ivanna.omega.audio

import java.util.Locale
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Telemetría en vivo 1 Hz (v2.5.407, IVANNA.Telemetry).
 * formatTelemetryLine es top-level y pura: se testea en host sin Android.
 * El contrato es el formato `clave=valor` que Esse parsea en Termux con
 * `logcat -s IVANNA.Telemetry` para calibrar la firma magistral.
 */
class IvannaTelemetryTest {

    private fun fullSnapshot() = TelemetrySnapshot(
        route = "A",
        asrcAvailable = true,
        ppm = 12.3,
        psiEta = 0.73,
        psiCoherence = 0.80,
        psiActive = true,
        orbe = 1.00f,
        holos = 0.50f,
        phi = 0.50f,
        volterra = 1.0f,
        shedFrac = 0.034f,
        shedEmaUs = 5459f,
        shedPeakUs = 17462f,
        shedPeakSessionUs = 117268f,
        pllDriftPpm = -500.0,
        pllOccupancyFrames = 2400L,
        pllTargetOccupancyFrames = 1920L,
        pllPhaseError = 0.25,
        pllDemandedPpm = -500.0,
        pllAtRail = true,
        pllStarvationHolds = 7L,
        gateHolosTier = 1,
        gateHolosCoherence = 0.85f,
        gateHolosWet = 0.46f,
        gateOrbeCoherence = 0.90f,
        gateOrbeShed = true,
        gateOrbeWet = 0.46f,
        rms = 0.12f,
        peak = 0.45f,
        gainReductionDb = -2.1f,
        targetGain = 1.5f,
        compAmount = 0.20f,
        excReduction = 0.10f,
        spatialWidth = 1.10f,
        safetyMargin = 0.90f,
        voiceProtect = 0.00f,
        applied = 1f,
        bandLo = 0.30f,
        bandMid = 0.50f,
        bandHi = 0.20f,
        npeGenre = "rock",
        mix = 0.60f
    )

    @Test fun fullSnapshotFormatsExactLine() {
        val expected = "route=A asrc=1 ppm=+12.3 psi_eta=0.73 psi_coh=0.80 psi_active=1 " +
            "pll_drift=-500.0 pll_occ=2400 pll_ph=+0.25 pll_dem=-500.0 pll_rail=1 pll_hold=7 " +
            "orbe=1.00 holos=0.50 phi=0.50 volterra=1 mix=0.60 " +
            "gate_holos_tier=1 gate_holos_coh=0.85 gate_holos_wet=0.46 " +
            "gate_orbe_coh=0.90 gate_orbe_shed=1 gate_orbe_wet=0.46 " +
            "shed=0.034 dsp_us=5459 dsp_pk=17462 dsp_pk_ses=117268 " +
            "rms=0.12 peak=0.45 gr_db=-2.10 gain_db=1.50 comp=0.20 exc=0.10 " +
            "width=1.10 safety=0.90 voice=0.00 applied=1 " +
            "band_lo=0.30 band_mid=0.50 band_hi=0.20 npe_genre=rock"
        assertEquals(expected, formatTelemetryLine(fullSnapshot()))
    }

    @Test fun mixFormatsValueAndDashWhenNotApplicable() {
        // v2.5.412: `mix=` es la ganancia de mezcla aplicada en captura.
        // En BridgePlayer no aplica (NaN) y se imprime como `--`.
        assertTrue(formatTelemetryLine(fullSnapshot()).contains("mix=0.60"))
        assertTrue(formatTelemetryLine(TelemetrySnapshot()).contains("mix=--"))
        assertTrue(formatTelemetryLine(fullSnapshot().copy(mix = 1.0f)).contains("mix=1.00"))
        assertTrue(formatTelemetryLine(fullSnapshot().copy(mix = 0.20f)).contains("mix=0.20"))
    }

    @Test fun shedFieldsFormatAsMeasured() {
        // v2.5.410: el shed es el dato que confirma si la cadena alcanza
        // el periodo (3.4% en la captura 2026-11-11 19:09, EMA 5459 µs).
        val line = formatTelemetryLine(fullSnapshot())
        assertTrue(line.contains("shed=0.034 dsp_us=5459 dsp_pk=17462 dsp_pk_ses=117268"))
        assertTrue(formatTelemetryLine(TelemetrySnapshot()).contains("shed=0.000 dsp_us=0 dsp_pk=0 dsp_pk_ses=0"))
    }

    @Test fun pllRailAndGateFieldsFormatAsMeasured415() {
        // v2.5.415, banderas 1 y 5 de la 414: la línea dice si el servo
        // está en el riel y por qué ORBE/HOLOS están en bypass.
        val line = formatTelemetryLine(fullSnapshot())
        assertTrue(line.contains("pll_drift=-500.0 pll_occ=2400 pll_ph=+0.25 pll_dem=-500.0 pll_rail=1 pll_hold=7"))
        assertTrue(line.contains("gate_holos_tier=1 gate_holos_coh=0.85 gate_holos_wet=0.46"))
        assertTrue(line.contains("gate_orbe_coh=0.90 gate_orbe_shed=1 gate_orbe_wet=0.46"))
        val def = formatTelemetryLine(TelemetrySnapshot())
        assertTrue(def.contains("pll_drift=+0.0 pll_occ=0 pll_ph=+0.00 pll_dem=+0.0 pll_rail=0 pll_hold=0"))
        assertTrue(def.contains("gate_holos_tier=0 gate_holos_coh=1.00 gate_holos_wet=0.00"))
        assertTrue(def.contains("gate_orbe_coh=1.00 gate_orbe_shed=0 gate_orbe_wet=0.00"))
    }

    @Test fun ppmAlwaysSignedOneDecimal() {
        assertTrue(formatTelemetryLine(fullSnapshot().copy(ppm = -12.3)).contains("ppm=-12.3"))
        assertTrue(formatTelemetryLine(fullSnapshot().copy(ppm = 0.0)).contains("ppm=+0.0"))
        assertTrue(formatTelemetryLine(fullSnapshot().copy(ppm = -500.0)).contains("ppm=-500.0"))
    }

    @Test fun defaultSnapshotMeansSilentAndOff() {
        val line = formatTelemetryLine(TelemetrySnapshot())
        assertTrue(line.startsWith("route=A asrc=0 ppm=+0.0 psi_eta=0.50 psi_coh=0.50 psi_active=0"))
        assertTrue(line.contains("orbe=0.00 holos=0.00 phi=0.00 volterra=0"))
        assertTrue(line.contains("rms=0.00 peak=0.00"))
        assertTrue(line.endsWith("npe_genre=none"))
    }

    @Test fun genreIsSanitizedForParsing() {
        assertTrue(formatTelemetryLine(fullSnapshot().copy(npeGenre = "hard rock")).endsWith("npe_genre=hard_rock"))
        assertTrue(formatTelemetryLine(fullSnapshot().copy(npeGenre = "  ")).endsWith("npe_genre=none"))
        assertTrue(formatTelemetryLine(fullSnapshot().copy(npeGenre = "rock=pop")).endsWith("npe_genre=rock_pop"))
    }

    @Test fun volterraAndAppliedAreOnOffInts() {
        assertTrue(formatTelemetryLine(fullSnapshot().copy(volterra = 0.0f)).contains("volterra=0"))
        assertTrue(formatTelemetryLine(fullSnapshot().copy(applied = 1234f)).contains("applied=1234"))
    }

    @Test fun localeWithCommaDecimalStillUsesDots() {
        // En un teléfono con locale es-ES, "%.2f" sin Locale daría "0,73"
        // y rompería el parseo. El formateador usa Locale.US siempre.
        val prev = Locale.getDefault()
        try {
            Locale.setDefault(Locale.GERMANY)
            val line = formatTelemetryLine(fullSnapshot())
            assertTrue("no debe haber comas decimales: $line", !line.contains(","))
            assertTrue(line.contains("psi_eta=0.73"))
        } finally {
            Locale.setDefault(prev)
        }
    }
}
