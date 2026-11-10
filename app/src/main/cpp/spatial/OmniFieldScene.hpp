// OmniFieldScene.hpp — OMNI-FIELD: separación, escena y personalización MAA.
// ============================================================================
// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// Parte del operador espacial unificado OMNI-FIELD (header-only). Este archivo
// cubre separación por fuentes + escena auto-supervisada + personalización
// HRTF por MAA. El SIREN/renderer vive en headers separados; el coordinador
// los ensambla en OmniField.hpp. Este header es autocontenido: no depende de
// ningún otro header de OMNI-FIELD.
//
// Diseño (reimplementación limpia, espejo del enfoque heurístico de
// ivanna_neural_upmixer.hpp / StereoObjectDecomposer.hpp / HrtfPersonalizer.hpp,
// sin copiar su código):
//   * Separación por crossover de 1 polo + envolvente de transitorios.
//   * Escena inferida de la propia señal (energías mid/side + correlación),
//     sin etiquetas ni modelo externo.
//   * Personalización por audiograma, con gradiente escalado por MAA.
//
// Reglas de tiempo real: C++17 portable, header-only, `noexcept` en el camino
// de audio, cero allocations, cero excepciones, todo el estado en arrays
// miembro fijos (std::array / arrays C de tamaño fijo).
// ============================================================================
#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace ivanna::omnifield {

// ---------------------------------------------------------------------------
// StemSeparator — estéreo -> 4 stems mono (VOCALS / DRUMS / BASS / OTHER)
// ---------------------------------------------------------------------------
// Método (por muestra, sobre el mid m = 0.5 * (L + R)):
//   bass     = LP 1 polo a 150 Hz de m.
//   vocalRaw = LP(4 kHz) de HP300^2(m): dos secciones HP de 1 polo a
//              300 Hz en cascada (cada una: x - LP300(x)) seguidas de un
//              LP a 4 kHz. La cascada da ~-20 dB a 100 Hz (un solo HP de
//              1 polo dejaría pasar ~-10 dB y contaminaría VOCALS con
//              graves); en banda (1 kHz) pasa ~0.89.
//   rem      = m - bass - vocalRaw           (resto fuera de banda).
//   hp       = m - LP(800 Hz) de m; envolvente hpEnv = LP 1 polo (tau
//              ~2 ms) de |hp|, y onset por flujo de envolvente:
//              onset = hpEnv - hpEnv retardada 10 ms (línea de retardo
//              fija de 2048 muestras, sin alloc). En régimen estacionario
//              hpEnv es constante y el onset tiende a 0 en ~15 ms (una
//              vocal sostenida a 1 kHz no se clasifica como batería,
//              aunque |hp| rice: el alisado lo elimina y el retardo está
//              en fase con el rizado residual); en un transitorio hpEnv
//              sube en ~2 ms mientras la versión retardada sigue baja
//              (gate -> 1 durante ~10-20 ms). Es un detector de onset por
//              envolvente, no por nivel.
//   gate     = clamp(onset / 0.10, 0, 1).
//   drums    = gate * (m - bass)             (parte transitoria > 150 Hz).
//   vocals   = (1 - gate) * vocalRaw         (banda, no transitoria).
//   other    = m - bass - drums - vocals     (RESIDUAL, ver garantía).
//
// GARANTÍA DE PRESERVACIÓN DEL MIX: por construcción algebraica,
//   bass + drums + vocals + other = m = 0.5 * (L + R)
// en cada muestra, porque `other` es el residual exacto. La única
// desviación posible es el redondeo float (~1e-6 absoluto por muestra en
// niveles normalizados) y el soft-clamp de `other` (ver abajo): el clamp es
// identidad para |other| <= 1.0, así que con audio normalizado (|m| <= 1)
// la suma es exacta salvo redondeo. Si |other| > 1 el clamp satura suave
// hacia un techo de 1.5 (|other_clamped| <= 1.5) y la suma se desvía
// exactamente en |other - other_clamped| (< |other| - 1 en ese caso).
// Los stems son mono (mezcla mid-dominante de cada componente); el llamador
// los renderiza como fuentes puntuales.
// ---------------------------------------------------------------------------
struct StemSeparator {
    // Orden de stems (coincide con ivanna::ai::StemType): s0..s3 en separate().
    enum Stem : int { kVocals = 0, kDrums = 1, kBass = 2, kOther = 3, kNumStems = 4 };

    void prepare(float sampleRate) noexcept {
        sampleRate_ = (std::isfinite(sampleRate) && sampleRate > 0.0f)
                          ? sampleRate
                          : 48000.0f;
        a150_   = onePoleFromFc(150.0f, sampleRate_);
        a300_   = onePoleFromFc(300.0f, sampleRate_);
        a4000_  = onePoleFromFc(4000.0f, sampleRate_);
        aTrans_ = onePoleFromFc(800.0f, sampleRate_);
        aHpEnv_ = onePoleFromTau(0.002f, sampleRate_);  // ~2 ms
        delaySamples_ = static_cast<int>(0.010f * sampleRate_ + 0.5f);
        if (delaySamples_ < 1) delaySamples_ = 1;
        if (delaySamples_ > 2047) delaySamples_ = 2047;
        reset();
    }

    void reset() noexcept {
        lp150_ = 0.0f;
        lp300a_ = 0.0f;
        lp300b_ = 0.0f;
        lp4000_ = 0.0f;
        lpTrans_ = 0.0f;
        hpEnv_ = 0.0f;
        hpEnvHist_.fill(0.0f);
        histIdx_ = 0;
        lastGate_ = 0.0f;
    }

    float sampleRate() const noexcept { return sampleRate_; }
    // Último gate de transitorios (0 = estacionario, 1 = transitorio pleno).
    float transientGate() const noexcept { return lastGate_; }

    // Separa un bloque planar L/R en 4 stems mono. Cualquier n > 0.
    // Punteros nulos o n <= 0: no hace nada. Sin allocations.
    void separate(const float* inL, const float* inR, int n,
                  float* s0, float* s1, float* s2, float* s3) noexcept {
        if (inL == nullptr || inR == nullptr || s0 == nullptr || s1 == nullptr ||
            s2 == nullptr || s3 == nullptr || n <= 0) {
            return;
        }
        for (int i = 0; i < n; ++i) {
            const float l = std::isfinite(inL[i]) ? inL[i] : 0.0f;
            const float r = std::isfinite(inR[i]) ? inR[i] : 0.0f;
            const float m = 0.5f * (l + r);

            lp150_ += a150_ * (m - lp150_);
            const float bass = lp150_;

            // Banda vocal 300 Hz - 4 kHz: HP300 de 1 polo en cascada x2 + LP 4 kHz.
            lp300a_ += a300_ * (m - lp300a_);
            const float hpA = m - lp300a_;
            lp300b_ += a300_ * (hpA - lp300b_);
            const float hpB = hpA - lp300b_;
            lp4000_ += a4000_ * (hpB - lp4000_);
            const float vocalRaw = lp4000_;

            // Detector de onset: HP (800 Hz) + flujo de envolvente.
            lpTrans_ += aTrans_ * (m - lpTrans_);
            const float hp = m - lpTrans_;
            hpEnv_ += aHpEnv_ * (std::fabs(hp) - hpEnv_);
            const int readIdx =
                (histIdx_ - delaySamples_ + 2048) & 2047;
            const float hpEnvDelayed = hpEnvHist_[readIdx];
            hpEnvHist_[histIdx_] = hpEnv_;
            histIdx_ = (histIdx_ + 1) & 2047;
            float onset = hpEnv_ - hpEnvDelayed;
            if (onset < 0.0f) onset = 0.0f;
            const float gate = std::clamp(onset * 10.0f, 0.0f, 1.0f);  // / 0.10
            lastGate_ = gate;

            const float drums = gate * (m - bass);
            const float vocals = (1.0f - gate) * vocalRaw;
            // Residual exacto: la suma de los 4 stems reproduce m.
            float other = m - bass - drums - vocals;
            other = softClamp(other);

            s0[i] = vocals;
            s1[i] = drums;
            s2[i] = bass;
            s3[i] = other;
        }
        // Evita denormales tras silencios largos (estados 1 polo).
        flushDenormals();
    }

private:
    static float onePoleFromFc(float fcHz, float sr) noexcept {
        return 1.0f - std::exp(-2.0f * 3.141592653589793f * fcHz / sr);
    }
    static float onePoleFromTau(float tauSec, float sr) noexcept {
        return 1.0f - std::exp(-1.0f / (tauSec * sr));
    }

    // Soft-clamp: identidad para |x| <= 1, saturación suave con techo 1.5.
    // y = sign * (1 + 0.5 * (1 - 1 / (1 + (|x| - 1)))) para |x| > 1.
    static float softClamp(float x) noexcept {
        const float ax = std::fabs(x);
        if (ax <= 1.0f) return x;
        const float d = ax - 1.0f;
        const float y = 1.0f + 0.5f * (1.0f - 1.0f / (1.0f + d));
        return (x < 0.0f) ? -y : y;
    }

    void flushDenormals() noexcept {
        if (std::fabs(lp150_) < 1e-30f) lp150_ = 0.0f;
        if (std::fabs(lp300a_) < 1e-30f) lp300a_ = 0.0f;
        if (std::fabs(lp300b_) < 1e-30f) lp300b_ = 0.0f;
        if (std::fabs(lp4000_) < 1e-30f) lp4000_ = 0.0f;
        if (std::fabs(lpTrans_) < 1e-30f) lpTrans_ = 0.0f;
        if (std::fabs(hpEnv_) < 1e-30f) hpEnv_ = 0.0f;
    }

    float sampleRate_ = 48000.0f;
    float a150_ = 0.0f, a300_ = 0.0f, a4000_ = 0.0f, aTrans_ = 0.0f;
    float aHpEnv_ = 0.0f;
    int delaySamples_ = 480;  // 10 ms a 48 kHz
    float lp150_ = 0.0f, lp300a_ = 0.0f, lp300b_ = 0.0f, lp4000_ = 0.0f, lpTrans_ = 0.0f;
    float hpEnv_ = 0.0f;
    // Línea de retardo de la envolvente (2048 muestras, potencia de 2).
    std::array<float, 2048> hpEnvHist_{};
    int histIdx_ = 0;
    float lastGate_ = 0.0f;
};

// ---------------------------------------------------------------------------
// SceneEstimator — escena auto-supervisada ligera (sin etiquetas)
// ---------------------------------------------------------------------------
// Por bloque acumula energías mid/side y correlación cruzada normalizada
// a lag 0 (mid = 0.5*(L+R), side = 0.5*(L-R)):
//   azimuthDeg(): sideRatio = sideE / (midE + sideE), signo por (E_L - E_R)
//     (+ = derecha, - = izquierda), mapeo a +/-60 deg. Suavizado 1 polo
//     con alpha = 0.05 por bloque: azimuth += 0.05 * (inst - azimuth).
//     0 deg = centro/mono, +/-60 deg = escena totalmente lateral.
//   diffuseness(): 1 - |corr|, 0 = direccional (L/R idénticos), 1 = difuso
//     (L/R incorrelados). Es el valor del último bloque procesado (sin
//     suavizado: la fórmula es literal por bloque). Antes del primer update
//     vale 0 (sin evidencia de difusión; correlación neutra = 1).
// ---------------------------------------------------------------------------
struct SceneEstimator {
    static constexpr float kAzimuthMaxDeg = 60.0f;
    static constexpr float kAzimuthSmoothAlpha = 0.05f;

    void prepare(float sampleRate) noexcept {
        sampleRate_ = (std::isfinite(sampleRate) && sampleRate > 0.0f)
                          ? sampleRate
                          : 48000.0f;
        reset();
    }

    void reset() noexcept {
        azimuthDeg_ = 0.0f;
        lastCorr_ = 1.0f;       // neutro: sin datos, correlación perfecta
        lastDiffuseness_ = 0.0f;
        lastSideRatio_ = 0.0f;
    }

    float sampleRate() const noexcept { return sampleRate_; }

    // Actualiza la escena con un bloque planar L/R. n <= 0 o nulos: no-op.
    void update(const float* l, const float* r, int n) noexcept {
        if (l == nullptr || r == nullptr || n <= 0) return;
        double eL = 0.0, eR = 0.0, eMid = 0.0, eSide = 0.0, sumLR = 0.0;
        for (int i = 0; i < n; ++i) {
            const float li = std::isfinite(l[i]) ? l[i] : 0.0f;
            const float ri = std::isfinite(r[i]) ? r[i] : 0.0f;
            const float mid = 0.5f * (li + ri);
            const float side = 0.5f * (li - ri);
            eL += static_cast<double>(li) * li;
            eR += static_cast<double>(ri) * ri;
            eMid += static_cast<double>(mid) * mid;
            eSide += static_cast<double>(side) * side;
            sumLR += static_cast<double>(li) * ri;
        }
        const double invN = 1.0 / static_cast<double>(n);
        eL *= invN;
        eR *= invN;
        eMid *= invN;
        eSide *= invN;
        sumLR *= invN;

        const double denom = std::sqrt(eL * eR);
        float corr = 1.0f;
        if (denom > 1e-12) {
            corr = static_cast<float>(std::clamp(sumLR / denom, -1.0, 1.0));
        }
        lastCorr_ = corr;
        lastDiffuseness_ = 1.0f - std::fabs(corr);

        const float sideRatio =
            static_cast<float>(eSide / (eMid + eSide + 1e-12));
        lastSideRatio_ = sideRatio;
        float sign = 0.0f;
        if (eL > eR) {
            sign = -1.0f;  // más energía a la izquierda
        } else if (eR > eL) {
            sign = 1.0f;  // más energía a la derecha
        }
        const float inst = sign * sideRatio * kAzimuthMaxDeg;
        azimuthDeg_ += kAzimuthSmoothAlpha * (inst - azimuthDeg_);
    }

    // Azimut dominante suavizado, en grados: + = derecha, - = izquierda.
    float azimuthDeg() const noexcept { return azimuthDeg_; }
    // 0 = direccional, 1 = difuso (último bloque).
    float diffuseness() const noexcept { return lastDiffuseness_; }
    // Accesores de diagnóstico (último bloque).
    float correlation() const noexcept { return lastCorr_; }
    float sideRatio() const noexcept { return lastSideRatio_; }

private:
    float sampleRate_ = 48000.0f;
    float azimuthDeg_ = 0.0f;
    float lastCorr_ = 1.0f;
    float lastDiffuseness_ = 0.0f;
    float lastSideRatio_ = 0.0f;
};

// ---------------------------------------------------------------------------
// MaaPersonalizer — personalización HRTF con gradiente escalado por MAA
// ---------------------------------------------------------------------------
// 8 sectores de azimut centrados en 0, 45, 90, 135, 180, 225, 270, 315 deg.
// El MAA (mínimo ángulo audible) es fino al frente y grueso atrás, simétrico:
//
//   sector:        0    1    2    3    4    5    6    7
//   ángulo (deg):  0   45   90  135  180  225  270  315
//   MAA (deg):     1    2    3    5   10   10    5    3
//   diezmado:      1    1    2    2    4    4    2    2  (bloques entre
//                                                        refrescos)
//   banda carac.: 8k   8k   4k   2k   2k   2k   4k   8k  (frontales 8 kHz,
//                                                        laterales 4 kHz,
//                                                        traseros 2 kHz)
//
// Diezmado: un sector con diezmado D se refresca cuando
// (blockIndex % D == 0). Actualizaciones medias por bloque:
//   2 sectores x 1   + 4 sectores x 1/2 + 2 sectores x 1/4
//   = 2 + 2 + 0.5 = 4.5 de 8  ->  1 - 4.5/8 = 43.75% ~ 44% menos cómputo
//   que refrescar los 8 sectores cada bloque (dentro del 30-52% del paper).
//
// Fórmulas (lossL/lossR: pérdidas dB >= 0 a 250/500/1k/2k/4k/8k Hz):
//   warpDeg(sector) = clamp(0.5 * (lossL[4k] - lossR[4k]) * cos(ángulo),
//                           -10, +10) deg; 0 si no hay asimetría.
//   gainDb(sector)  = -clamp(pérdida media en la banda característica
//                           del sector, 0, 6) dB (compensación parcial,
//                           capada a 6 dB para no sobreamplificar).
//   redesignThresholdDeg(sector) = clamp(MAA del sector, 0.5, 10) deg:
//     umbral de rediseño del FIR: solo se rediseña si el cambio de azimut
//     supera el MAA de ese sector.
// Sectores fuera de [0, 7] devuelven 0 (fail-closed).
// ---------------------------------------------------------------------------
struct MaaPersonalizer {
    static constexpr int kNumSectors = 8;
    static constexpr int kNumBands = 6;  // 250/500/1k/2k/4k/8k Hz
    static constexpr int kBand4k = 4;
    static constexpr int kBand2k = 3;
    static constexpr int kBand8k = 5;

    static constexpr std::array<int, kNumSectors> kSectorAngleDeg = {
        0, 45, 90, 135, 180, 225, 270, 315};
    // MAA por sector (grados): frontal fino, trasero grueso, simétrica.
    static constexpr std::array<float, kNumSectors> kMaaDeg = {
        1.0f, 2.0f, 3.0f, 5.0f, 10.0f, 10.0f, 5.0f, 3.0f};
    // Diezmado por sector (bloques entre refrescos).
    static constexpr std::array<int, kNumSectors> kDecimationBlocks = {
        1, 1, 2, 2, 4, 4, 2, 2};
    // Banda característica por sector (índice en el audiograma).
    static constexpr std::array<int, kNumSectors> kCharBandIndex = {
        kBand8k, kBand8k, kBand4k, kBand2k,
        kBand2k, kBand2k, kBand4k, kBand8k};
    // cos(ángulo del sector), precalculado (evita cos() en el camino de uso).
    static constexpr std::array<float, kNumSectors> kCosSector = {
        1.0f, 0.70710678f, 0.0f, -0.70710678f,
        -1.0f, -0.70710678f, 0.0f, 0.70710678f};

    void prepare(float sampleRate) noexcept {
        sampleRate_ = (std::isfinite(sampleRate) && sampleRate > 0.0f)
                          ? sampleRate
                          : 48000.0f;
        reset();
    }

    void reset() noexcept {
        lossL_.fill(0.0f);
        lossR_.fill(0.0f);
        updateCount_.fill(0);
        totalUpdates_ = 0;
    }

    float sampleRate() const noexcept { return sampleRate_; }

    // Pérdidas dB (>= 0) a 250/500/1k/2k/4k/8k Hz, por oído.
    // Valores negativos se clampan a 0; > 120 dB a 120 dB; no finitos a 0.
    // Punteros nulos: no hace nada.
    void setAudiogram(const float lossL[kNumBands],
                      const float lossR[kNumBands]) noexcept {
        if (lossL == nullptr || lossR == nullptr) return;
        for (int i = 0; i < kNumBands; ++i) {
            lossL_[i] = sanitizeLoss(lossL[i]);
            lossR_[i] = sanitizeLoss(lossR[i]);
        }
    }

    // Refresco con diezmado por MAA: llamar una vez por bloque con el
    // índice global de bloque (0, 1, 2, ...). Solo se refrescan (y cuentan)
    // los sectores con (blockIndex % diezmado == 0). blockIndex < 0: no-op.
    void update(int blockIndex) noexcept {
        if (blockIndex < 0) return;
        for (int s = 0; s < kNumSectors; ++s) {
            if (blockIndex % kDecimationBlocks[s] == 0) {
                ++updateCount_[s];
                ++totalUpdates_;
            }
        }
    }

    // Warp de azimut por sector, por asimetría interaural a 4 kHz (grados).
    float warpDeg(int sector) const noexcept {
        if (sector < 0 || sector >= kNumSectors) return 0.0f;
        const float asym = lossL_[kBand4k] - lossR_[kBand4k];
        return std::clamp(0.5f * asym * kCosSector[sector], -10.0f, 10.0f);
    }

    // Compensación de la pérdida auditiva en la banda del sector (dB, <= 0).
    float gainDb(int sector) const noexcept {
        if (sector < 0 || sector >= kNumSectors) return 0.0f;
        const int band = kCharBandIndex[sector];
        const float meanLoss = 0.5f * (lossL_[band] + lossR_[band]);
        return -std::clamp(meanLoss, 0.0f, 6.0f);
    }

    // Umbral de rediseño del FIR = MAA del sector, clamp [0.5, 10] deg.
    float redesignThresholdDeg(int sector) const noexcept {
        if (sector < 0 || sector >= kNumSectors) return 0.0f;
        return std::clamp(kMaaDeg[sector], 0.5f, 10.0f);
    }

    int updateCount(int sector) const noexcept {
        if (sector < 0 || sector >= kNumSectors) return 0;
        return updateCount_[sector];
    }
    int totalUpdates() const noexcept { return totalUpdates_; }

private:
    static float sanitizeLoss(float v) noexcept {
        if (!std::isfinite(v)) return 0.0f;
        return std::clamp(v, 0.0f, 120.0f);
    }

    float sampleRate_ = 48000.0f;
    std::array<float, kNumBands> lossL_{};
    std::array<float, kNumBands> lossR_{};
    std::array<int, kNumSectors> updateCount_{};
    int totalUpdates_ = 0;
};

}  // namespace ivanna::omnifield
