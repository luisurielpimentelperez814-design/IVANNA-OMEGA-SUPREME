#pragma once
//
// DspLoadShedGovernor — Gobernador de shed de carga del DSP (Ruta A).
//
// Problema medido en vivo (Moto G85, 2026-11-09, logcat CaptureEngine):
//   dsp_bridge = 3.5–8.9 ms por bloque de 320 frames @48 kHz, presupuesto
//   6.67 ms. La cadena nativa completa (EQ/Comp/Exciter/Widener, EvoEq,
//   ORBE + HOLOS — cada uno STFT 512/hop 128, ~10–14 FFT por bloque —,
//   Hiper-Operador Phi 24 bandas, PDEngine, WFS, RIR por convolucion
//   particionada, Volterra, coclear, pipeline unificado, 5 ejes supremos,
//   NPE, limiter) corre en UN SOLO hilo — el mismo que lee de AudioRecord.
//   En un Snapdragon 6s Gen 3 (2x A78 + 6x A55) el mismo codigo tarda
//   ~2.5x mas en un core pequeno que en uno grande, y el planificador EAS
//   migra el hilo entre ambos; a eso se suman etapas cuyo coste depende
//   del contenido (particiones activas de la cola RIR, rutas HRTF/upmix)
//   y la preemption por otros hilos. Resultado: carga sostenida 100–105%,
//   el anillo de AudioRecord (8 bloques = 53 ms) se drena en ~1 s y el
//   RecordThread se desborda -> dropouts. Subir el buffer no lo arregla
//   (una sobrecarga SOSTENIDA solo retrasa el desborde); el aviso de la
//   374 y el decimado del NPE solo absorben transitorios.
//
// Solucion: shed por DEUDA. El DSP dispone de un presupuesto por bloque
// (= 70% del periodo, el mismo margen de seguridad que
// AudioThreadBudgetGuard.SAFETY_MARGIN_RATIO en Kotlin; el 30% restante
// queda para NPE, etapa de salida, reinyeccion, escritura bloqueante a
// AudioTrack y telemetria). Cada bloque procesado suma a la deuda su
// sobrecoste (coste - presupuesto); cada bloque en shed la reduce en un
// presupuesto entero. Mientras haya deuda, el bloque se pasa en seco
// (passthrough). En regimen, el ciclo de trabajo converge exactamente a
// la fraccion sostenible presupuesto/coste (p. ej. coste 8.9 ms ->
// ~52% de bloques procesados, ~48% en passthrough), asi que la carga
// media del DSP nunca supera el presupuesto y la lectura nunca se retrasa
// mas de lo que el anillo absorbe. La deuda esta acotada a [-presupuesto,
// +presupuesto]: un pico aislado (GC, 20 ms) cuesta como mucho 1 bloque
// en shed, y tras una racha buena solo se toleran ~1 presupuesto de
// rafaga lenta antes de volver a soltar.
//
// Fail-closed, por diseno:
//   - Shed = NO TOCAR el buffer: la senal seca sigue sonando por su via
//     y la reinyeccion diferencial aporta ~0 (delta = procesado - seco).
//     Jamas silencia ni pierde muestras; solo degrada el procesado.
//   - Se salta la llamada COMPLETA (igual que el early-return que ya
//     existe cuando otro hilo tiene g_dspProcessMutex): ningun estado
//     interno (IIR, STFT, convolver) queda avanzado a medias.
//   - Sin malloc, sin mutex, sin I/O, sin reloj de pared en el hot path
//     salvo dos lecturas de steady_clock por bloque en el llamador.
//   - La EMA solo mide la DEMANDA (bloques procesados): los bloques en
//     shed no la contaminan, asi que la telemetria sigue diciendo cuanto
//     cuesta el DSP de verdad.
//
// Hilo: una sola instancia por ruta, tocada solo bajo g_dspProcessMutex
// por el hilo de audio de Ruta A (igual que el resto del estado de
// nativeProcess). No es thread-safe por si misma a proposito: cero
// overhead de sincronizacion en el hot path.
//
// Tests host: dsp_core_stability.cpp (DspLoadShedGovernor.*) — reproduce
// el rango medido 3.5–8.9 ms y verifica que el shed acota la carga media
// al presupuesto sin silenciar nunca.
//

#include <algorithm>
#include <cstdint>

namespace ivanna::dsp {

class DspLoadShedGovernor {
public:
    // Presupuesto del DSP por bloque, como fraccion del periodo:
    // 70% — mismo margen que AudioThreadBudgetGuard.SAFETY_MARGIN_RATIO.
    static constexpr int64_t kBudgetNumerator = 7;
    static constexpr int64_t kBudgetDenominator = 10;

    static int64_t dspBudgetUs(int64_t periodUs) noexcept {
        if (periodUs <= 0) return 0;
        return periodUs * kBudgetNumerator / kBudgetDenominator;
    }

    // true mientras haya deuda pendiente: este bloque debe ir en seco.
    bool shouldShed(int64_t periodUs) const noexcept {
        if (periodUs <= 0) return false;
        return debtUs_ > 0;
    }

    // Bloque procesado: la deuda crece con el sobrecoste sobre el
    // presupuesto (negativa si el bloque fue mas rapido que el presupuesto).
    void observeRun(int64_t elapsedUs, int64_t periodUs) noexcept {
        if (periodUs <= 0 || elapsedUs < 0) return;
        const int64_t budgetUs = dspBudgetUs(periodUs);
        ++totalBlocks_;
        emaUs_ = (emaUs_ == 0) ? elapsedUs : emaUs_ + (elapsedUs - emaUs_) / 8;
        if (elapsedUs > peakUs_) peakUs_ = elapsedUs;
        debtUs_ += elapsedUs - budgetUs;
        clampDebt(budgetUs);
    }

    // Bloque en shed (passthrough, coste ~0): devuelve un presupuesto
    // entero de deuda. La EMA no se toca: sigue midiendo la demanda real.
    void observeShed(int64_t periodUs) noexcept {
        if (periodUs <= 0) return;
        const int64_t budgetUs = dspBudgetUs(periodUs);
        ++totalBlocks_;
        ++shedBlocks_;
        debtUs_ -= budgetUs;
        clampDebt(budgetUs);
    }

    void reset() noexcept {
        debtUs_ = 0;
        emaUs_ = 0;
        peakUs_ = 0;
        totalBlocks_ = 0;
        shedBlocks_ = 0;
    }

    int64_t debtUs() const noexcept { return debtUs_; }
    int64_t emaUs() const noexcept { return emaUs_; }
    int64_t peakUs() const noexcept { return peakUs_; }
    uint64_t totalBlocks() const noexcept { return totalBlocks_; }
    uint64_t shedBlocks() const noexcept { return shedBlocks_; }
    double shedFraction() const noexcept {
        return totalBlocks_ == 0 ? 0.0
                                 : static_cast<double>(shedBlocks_) /
                                       static_cast<double>(totalBlocks_);
    }

private:
    void clampDebt(int64_t budgetUs) noexcept {
        debtUs_ = std::clamp(debtUs_, -budgetUs, budgetUs);
    }

    int64_t debtUs_ = 0;       // >0: hay que soltar bloques. Acotada a ±presupuesto.
    int64_t emaUs_ = 0;        // EMA (1/8) del coste de los bloques PROCESADOS.
    int64_t peakUs_ = 0;       // Peor bloque procesado visto (telemetria).
    uint64_t totalBlocks_ = 0;
    uint64_t shedBlocks_ = 0;
};

}  // namespace ivanna::dsp
