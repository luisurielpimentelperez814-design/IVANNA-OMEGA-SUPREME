package com.ivanna.omega.audio

import android.util.Log
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.neuromorphic.IvannaNpeEngine
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import java.util.Locale

/**
 * Telemetría en vivo de TODOS los efectos de IVANNA, Ruta A (v2.5.407).
 *
 * REGLA #1 DE ESSE (2026-11-11): nada básico, magistral, riguroso, literal.
 * Esse escucha los efectos en Tidal y necesita la lectura en vivo completa
 * para calibrar la "firma magistral": una línea por segundo en logcat,
 * tag `IVANNA.Telemetry`, formato `clave=valor` parseable:
 *
 *   route=A asrc=1 ppm=+12.3 psi_eta=0.73 psi_coh=0.80 psi_active=1
 *   orbe=1.00 holos=0.50 phi=0.50 volterra=1 rms=0.12 peak=0.45
 *   gr_db=-2.10 gain_db=1.50 comp=0.20 exc=0.10 width=1.10 safety=0.90
 *   voice=0.00 applied=1 band_lo=0.30 band_mid=0.50 band_hi=0.20
 *   npe_genre=rock
 *
 * Fuentes (REUTILIZADAS, cero duplicación):
 *  - Telemetría adaptativa: IvannaNativeLib.nativeGetAdaptiveTelemetry()
 *    (10 floats: rms, peak, grDb, targetGain, compAmount, excReduction,
 *    spatialWidth, safetyMargin, voiceProtect, applied).
 *  - Bandas: nativeGetBandEnergies() (low/mid/high).
 *  - Amounts: nativeGetEffectAmounts() (orbe, holos, phi, volterra 1/0).
 *  - ASRC/Ψ: RouteAAsrc.isAvailable / clockCorrectionPpm (ppm APLICADO
 *    por el nativo) / psiEta / psiCoherence / psiActive.
 *  - NPE: IvannaNpeEngine.isReady + getDetectedGenre().
 *
 * Seguridad:
 *  - El poller corre en Dispatchers.Default (hilo de fondo), NUNCA en el
 *    hilo de audio RT. Solo mientras Ruta A reproduce: arranca con el
 *    decode loop de IvannaBridgePlayer y se cancela en su `finally`.
 *  - Todos los reads con runCatching y fail-closed: si algo falla, la
 *    línea se salta; el audio NUNCA se rompe por la telemetría.
 *  - Locale.US en el formateo: en un teléfono con locale es-ES, "%.2f"
 *    daría "0,73" con coma y rompería el parseo. Con US siempre es punto.
 *  - formatTelemetryLine es top-level y pura (sin Android en su camino):
 *    testeable en host (IvannaTelemetryTest). NO va dentro de un
 *    companion object: los símbolos ahí no resuelven desde los tests
 *    (lección dura del repo).
 */

/** Tag logcat para la lectura en vivo: `logcat -s IVANNA.Telemetry`. */
const val TELEMETRY_TAG = "IVANNA.Telemetry"

/** Un snapshot de todos los efectos en un instante. Valores por defecto = silencio/apagado. */
data class TelemetrySnapshot(
    val route: String = "A",
    val asrcAvailable: Boolean = false,
    val ppm: Double = 0.0,
    val psiEta: Double = 0.5,
    val psiCoherence: Double = 0.5,
    val psiActive: Boolean = false,
    val orbe: Float = 0f,
    val holos: Float = 0f,
    val phi: Float = 0f,
    val volterra: Float = 0f, // 1.0/0.0 (on/off, no tiene amount)
    val rms: Float = 0f,
    val peak: Float = 0f,
    val gainReductionDb: Float = 0f,
    val targetGain: Float = 0f,
    val compAmount: Float = 0f,
    val excReduction: Float = 0f,
    val spatialWidth: Float = 0f,
    val safetyMargin: Float = 0f,
    val voiceProtect: Float = 0f,
    val applied: Float = 0f,
    val bandLo: Float = 0f,
    val bandMid: Float = 0f,
    val bandHi: Float = 0f,
    val npeGenre: String = "none"
)

/**
 * Formatea un snapshot como una línea `clave=valor` parseable.
 * Pura y top-level: sin lecturas nativas, sin Android, testeable en host.
 * ppm con signo y 1 decimal; el resto de floats con 2 decimales.
 */
fun formatTelemetryLine(s: TelemetrySnapshot): String {
    val genre = s.npeGenre.ifBlank { "none" }.replace(Regex("[\\s=]+"), "_")
    return String.format(
        Locale.US,
        "route=%s asrc=%d ppm=%+.1f psi_eta=%.2f psi_coh=%.2f psi_active=%d " +
            "orbe=%.2f holos=%.2f phi=%.2f volterra=%d " +
            "rms=%.2f peak=%.2f gr_db=%.2f gain_db=%.2f comp=%.2f exc=%.2f " +
            "width=%.2f safety=%.2f voice=%.2f applied=%d " +
            "band_lo=%.2f band_mid=%.2f band_hi=%.2f npe_genre=%s",
        s.route,
        if (s.asrcAvailable) 1 else 0,
        s.ppm,
        s.psiEta,
        s.psiCoherence,
        if (s.psiActive) 1 else 0,
        s.orbe,
        s.holos,
        s.phi,
        if (s.volterra > 0.5f) 1 else 0,
        s.rms,
        s.peak,
        s.gainReductionDb,
        s.targetGain,
        s.compAmount,
        s.excReduction,
        s.spatialWidth,
        s.safetyMargin,
        s.voiceProtect,
        s.applied.toInt(),
        s.bandLo,
        s.bandMid,
        s.bandHi,
        genre
    )
}

/**
 * Lee un snapshot de todas las fuentes vivas. null si la librería nativa
 * no está cargada (el llamador salta la línea). Cada read va con
 * runCatching: un getter que falle deja su valor en 0, nunca rompe nada.
 */
fun collectTelemetrySnapshot(routeAAsrc: RouteAAsrc): TelemetrySnapshot? {
    val lib = IvannaNativeLib
    if (!lib.isLoaded) return null
    val tele = runCatching { lib.nativeGetAdaptiveTelemetry() }.getOrNull()
    val bands = runCatching { lib.nativeGetBandEnergies() }.getOrNull()
    val amounts = runCatching { lib.nativeGetEffectAmounts() }.getOrNull()
    val npeReady = IvannaNpeEngine.isReady
    val genre = if (npeReady) {
        runCatching { IvannaNpeEngine.getDetectedGenre() }.getOrNull()
    } else {
        null
    }
    return TelemetrySnapshot(
        route = "A",
        asrcAvailable = runCatching { routeAAsrc.isAvailable }.getOrDefault(false),
        ppm = runCatching { routeAAsrc.clockCorrectionPpm }.getOrDefault(0.0),
        psiEta = runCatching { routeAAsrc.psiEta }.getOrDefault(0.5),
        psiCoherence = runCatching { routeAAsrc.psiCoherence }.getOrDefault(0.5),
        psiActive = runCatching { routeAAsrc.psiActive }.getOrDefault(false),
        orbe = amounts?.getOrNull(0) ?: 0f,
        holos = amounts?.getOrNull(1) ?: 0f,
        phi = amounts?.getOrNull(2) ?: 0f,
        volterra = amounts?.getOrNull(3) ?: 0f,
        rms = tele?.getOrNull(0) ?: 0f,
        peak = tele?.getOrNull(1) ?: 0f,
        gainReductionDb = tele?.getOrNull(2) ?: 0f,
        targetGain = tele?.getOrNull(3) ?: 0f,
        compAmount = tele?.getOrNull(4) ?: 0f,
        excReduction = tele?.getOrNull(5) ?: 0f,
        spatialWidth = tele?.getOrNull(6) ?: 0f,
        safetyMargin = tele?.getOrNull(7) ?: 0f,
        voiceProtect = tele?.getOrNull(8) ?: 0f,
        applied = tele?.getOrNull(9) ?: 0f,
        bandLo = bands?.getOrNull(0) ?: 0f,
        bandMid = bands?.getOrNull(1) ?: 0f,
        bandHi = bands?.getOrNull(2) ?: 0f,
        npeGenre = genre?.takeIf { it.isNotBlank() } ?: "none"
    )
}

/**
 * Arranca el volcado 1 Hz en un hilo de fondo (Dispatchers.Default).
 * El llamador guarda el Job y lo cancela cuando la reproducción termina
 * (IvannaBridgePlayer lo cancela en el `finally` del decode loop).
 * El Job es hijo del scope que lo lanza: si el scope se cancela, el
 * poller muere con él. La cancelación durante delay() es normal.
 */
fun CoroutineScope.startTelemetryPoller(
    routeAAsrc: RouteAAsrc,
    intervalMs: Long = 1000L
): Job = launch(Dispatchers.Default) {
    while (isActive) {
        runCatching {
            val snapshot = collectTelemetrySnapshot(routeAAsrc)
            if (snapshot != null) Log.i(TELEMETRY_TAG, formatTelemetryLine(snapshot))
        }
        delay(intervalMs)
    }
}
