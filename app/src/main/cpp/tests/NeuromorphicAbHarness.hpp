// NeuromorphicAbHarness.hpp — Banco A/B del aporte audible del motor neuromórfico
// (c) 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// HUECO QUE CIERRA: el README admite que el motor neuromórfico (NHO, Volterra H2, pools LIF,
// upmixer neural) no tiene medición de su aporte audible. Sin eso es imposible saber si suma
// o solo colorea. Este banco hace la medición correcta y desnuda: PRUEBA DE NULL.
//
// MÉTODO (estándar de la industria, no inventado):
//   1. Se procesa la MISMA entrada por dos caminos: A (motor apagado, referencia) y
//      B (motor encendido).
//   2. Se estima el retardo de grupo por correlación cruzada (el motor puede tener
//      latencia de bloque) y se alinean las señales: sin alinear, la resta mide el retardo,
//      no el efecto.
//   3. Residuo r = B − A. Su energía frente a la señal es lo que el motor AÑADE.
//   4. Se reparte el residuo por bandas audibles (5 bandas del Atlas) porque un residuo
//      global de −50 dB puede ser inaudible en medios y perfectamente audible en agudos.
//   5. Se compara cada banda con el umbral de audibilidad relativo al nivel de la señal
//      en esa banda (proxy de enmascaramiento simultáneo, ~ −40 dB por banda en la zona
//      media): si el residuo queda por debajo, el motor es INAUDIBLE en esa banda.
//   6. Se comprueba también la coherencia de fase (correlación) para distinguir una
//      coloración de un simple retardo o de un cambio de ganancia.
//
// Esto no sustituye una escucha A/B humana (ABX): la complementa con números reproducibles
// y pone un piso duro — si el residuo es inaudible, cualquier "mejora" es sugestión.
//
// RT-safe por diseño de uso: measure() se llama en hilo de análisis, nunca en el de audio.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ivanna { namespace measure {

struct AbBand {
    const char* name = "";
    float loHz = 0.0f, hiHz = 0.0f;
    float energyA = 0.0f;        // energía de referencia (lineal)
    float residual = 0.0f;       // energía del residuo (lineal)
    float deltaDb = 0.0f;        // 10*log10(residuo/energía A) — cuánto añade en la banda
    bool  audible = false;       // ¿supera el umbral de audibilidad relativo?
};

struct AbReport {
    float delaySamples = 0.0f;   // retardo detectado entre A y B
    float residualRmsDb = 0.0f;  // residuo global frente a A (dB)
    float coherence = 1.0f;      // correlación A/B tras alineado [−1..1]
    float gainDb = 0.0f;         // cambio de ganancia puro (dB)
    bool  identical = false;     // B == A (residuo nulo)
    bool  audibleContribution = false;
    int   audibleBands = 0;
    AbBand bands[5]{};
    float hostSecondsPerSecond = 0.0f; // coste medido: segundos de host por segundo de audio
};

class AbHarness {
public:
    static constexpr float kAudibilityThresholdDb = -40.0f; // umbral relativo por banda
    static constexpr int   kMaxDelaySearch = 4096;          // búsqueda de retardo (±4096 muestras)

    // Banco de 5 bandas del Atlas: sub, cuerpo, definición, presencia, aire.
    void prepare(float sampleRate) noexcept {
        sr_ = (sampleRate > 8000.0f && std::isfinite(sampleRate)) ? sampleRate : 48000.0f;
        bands_[0] = {"sub",        20.0f,    80.0f};
        bands_[1] = {"cuerpo",     80.0f,   400.0f};
        bands_[2] = {"definición", 400.0f, 3000.0f};
        bands_[3] = {"presencia", 3000.0f, 8000.0f};
        bands_[4] = {"aire",      8000.0f, 20000.0f};
        // Coeficientes de un polo por banda (banco barato, cero asignaciones en measure()).
        for (int b = 0; b < 5; ++b) {
            aLow_[b]  = 1.0f - std::exp(-2.0f * 3.14159265f * bands_[b].hiHz / sr_);
            aHigh_[b] = 1.0f - std::exp(-2.0f * 3.14159265f * bands_[b].loHz / sr_);
            lp_[b] = 0.0f; hp_[b] = 0.0f;
        }
    }

    // Medición principal. A = referencia (motor apagado), B = motor encendido.
    AbReport measure(const float* A, const float* B, int n) noexcept {
        AbReport r{};
        if (!A || !B || n <= 0) return r;

        // 1) Energía de referencia y detección exacta de identidad (null perfecto).
        double eA = 0.0, eRes = 0.0, eB = 0.0; bool identical = true, finite = true;
        for (int i = 0; i < n; ++i) {
            const float a = A[i], b = B[i];
            if (!std::isfinite(a) || !std::isfinite(b)) { finite = false; continue; }
            if (a != b) identical = false;
            eA += (double)a * a;
            eB += (double)b * b;
            eRes += (double)(b - a) * (double)(b - a);
        }
        r.identical = identical && finite;
        if (eA <= 1.0e-20) { r.residualRmsDb = -200.0f; r.coherence = 1.0f; return r; }
        if (r.identical) { r.residualRmsDb = -200.0f; r.coherence = 1.0f; r.audibleContribution = false; return r; }

        // 2) Retardo de grupo por correlación cruzada normalizada.
        //    Búsqueda gruesa (±kMaxDelaySearch) + refinado ±16 + vecindad EXACTA de 0 (±64).
        //    El paso grueso (>= 8 muestras) puede saltarse un pico estrecho (señales con agudos
        //    o periódicas): sin la vecindad de 0 un retardo real de 0 se medía como -48 con
        //    coherencia 0.85 y un residuo de -70 dB salía "audible" en las 5 bandas. Ante un
        //    empate numérico (señal periódica) manda el retardo de menor magnitud.
        const int step = std::max(1, n / 4096);
        int bestLag = 0; double bestCorr = -2.0;
        auto consider = [&](int lag) noexcept {
            double num = 0.0, dA = 0.0, dB = 0.0;
            for (int i = 0; i < n; i += step) {
                const int j = i + lag;
                if (j < 0 || j >= n) continue;
                num += (double)A[i] * (double)B[j];
                dA  += (double)A[i] * (double)A[i];
                dB  += (double)B[j] * (double)B[j];
            }
            const double c = num / (std::sqrt(dA * dB) + 1.0e-30);
            if (c > bestCorr + 1.0e-9 ||
                (c > bestCorr - 1.0e-9 && std::abs(lag) < std::abs(bestLag))) {
                bestCorr = c; bestLag = lag;
            }
        };
        for (int lag = -kMaxDelaySearch; lag <= kMaxDelaySearch; lag += std::max(step, 8)) consider(lag);
        const int coarseBest = bestLag;
        for (int lag = coarseBest - 16; lag <= coarseBest + 16; ++lag) consider(lag);
        for (int lag = -64; lag <= 64; ++lag) consider(lag);
        r.delaySamples = (float)bestLag;
        r.coherence = (float)bestCorr;

        // 3) Residuo tras alinear (B desplazado bestLag respecto de A).
        double resEnergy = 0.0, refEnergy = 0.0, sigB = 0.0;
        int cnt = 0;
        for (int i = 0; i < n; ++i) {
            const int j = i + bestLag;
            if (j < 0 || j >= n) continue;
            const double a = (double)A[i], b = (double)B[j];
            resEnergy += (b - a) * (b - a);
            refEnergy += a * a;
            sigB += b * b;
            ++cnt;
        }
        if (cnt == 0 || refEnergy <= 1.0e-20) { r.residualRmsDb = -200.0f; return r; }
        r.residualRmsDb = 10.0f * std::log10(std::max(resEnergy / refEnergy, 1.0e-20));
        r.gainDb = 10.0f * std::log10(std::max(sigB / refEnergy, 1.0e-20));

        // 4) Reparto por bandas del Atlas (mismo banco de un polo para A y para el residuo).
        double bandRef[5] = {0, 0, 0, 0, 0}, bandRes[5] = {0, 0, 0, 0, 0};
        float lpRef[5] = {0, 0, 0, 0, 0}, hpRef[5] = {0, 0, 0, 0, 0};
        float lpRes[5] = {0, 0, 0, 0, 0}, hpRes[5] = {0, 0, 0, 0, 0};
        for (int b = 0; b < 5; ++b) { (void)lp_[b]; (void)hp_[b]; (void)aLow_[b]; (void)aHigh_[b]; }
        for (int i = 0; i < n; ++i) {
            const int j = i + bestLag;
            if (j < 0 || j >= n) continue;
            const float a = A[i], res = B[j] - A[i];
            for (int b = 0; b < 5; ++b) {
                lpRef[b] += aLow_[b] * (a - lpRef[b]);
                hpRef[b] += aHigh_[b] * (lpRef[b] - hpRef[b]);
                const float xa = lpRef[b] - hpRef[b];
                lpRes[b] += aLow_[b] * (res - lpRes[b]);
                hpRes[b] += aHigh_[b] * (lpRes[b] - hpRes[b]);
                const float xr = lpRes[b] - hpRes[b];
                bandRef[b] += (double)xa * xa;
                bandRes[b] += (double)xr * xr;
            }
        }
        int audible = 0;
        for (int b = 0; b < 5; ++b) {
            AbBand& bd = r.bands[b];
            bd.name = bands_[b].name;
            bd.loHz = bands_[b].loHz;
            bd.hiHz = bands_[b].hiHz;
            bd.energyA = (float)bandRef[b];
            bd.residual = (float)bandRes[b];
            bd.deltaDb = (bandRef[b] > 1.0e-20)
                       ? 10.0f * std::log10(std::max(bandRes[b] / bandRef[b], 1.0e-20))
                       : -200.0f;
            bd.audible = (bd.deltaDb > kAudibilityThresholdDb);
            if (bd.audible) ++audible;
        }
        r.audibleBands = audible;
        r.audibleContribution = (audible > 0) && (r.residualRmsDb > -60.0f);
        return r;
    }

    // Coste de cómputo del camino B: segundos de host por segundo de audio procesado.
    // Un valor < 1 significa margen en tiempo real; > 1 significa que no cabe.
    float hostCostRatio(uint64_t hostNanos, int frames) const noexcept {
        if (frames <= 0) return 0.0f;
        const double secondsOfAudio = (double)frames / (double)sr_;
        const double hostSeconds = (double)hostNanos * 1.0e-9;
        return (float)(hostSeconds / std::max(secondsOfAudio, 1.0e-12));
    }

private:
    struct BandDef { const char* name; float loHz; float hiHz; };
    float sr_ = 48000.0f;
    BandDef bands_[5]{};
    float aLow_[5]{}, aHigh_[5]{};
    float lp_[5]{}, hp_[5]{};
};

}} // namespace ivanna::measure
