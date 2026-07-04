// gammatone_lattice.hpp
/*
 * ============================================================================
 * IVANNA — Filterbank Gammatone 13 bandas, forma lattice (coupled-form)
 * ============================================================================
 * © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
 *
 * Motivación: gammatone_filterbank13.hpp (v1) implementa cada sección en
 * Direct Form II Transposed: y[n] = x[n] - a1*y[n-1] - a2*y[n-2]. Es correcta
 * y ya tiene ruta NEON, pero la sensibilidad a cuantización de a1 crece cerca
 * de fs/4 y con Q alto (bandas graves del banco, ERB angosto). La forma
 * lattice/coupled-form realiza el mismo polo complejo r·e^{±jw0} como una
 * rotación 2D pura (dos multiplicaciones + dos sumas, sin producto directo
 * por a1), lo que mantiene el módulo del vector de estado numéricamente más
 * estable en cascadas largas o bloques grandes — apropiado para el wallpaper
 * v2, que corre continuamente en el hilo de audio incluso con la app en
 * segundo plano.
 *
 *   y1[n] = r·cos(w0)·y1[n-1] − r·sin(w0)·y2[n-1] + x[n]
 *   y2[n] = r·sin(w0)·y1[n-1] + r·cos(w0)·y2[n-1]
 *   salida de la sección = y1[n]
 *
 * r y w0 se derivan de los mismos a1,a2 del diseño ERB (Slaney/Glasberg &
 * Moore) para que ambas formas modelen el mismo polo — solo cambia la
 * realización, no el diseño del filtro. No se elimina gammatone_filterbank13.hpp:
 * ambas conviven (v1 sigue alimentando el pipeline de 3 bandas colapsadas
 * bass/mid/high; v2 expone las 13 bandas crudas para el wallpaper PBR).
 * ============================================================================
 */
#pragma once
#include <array>
#include <cmath>
#include <algorithm>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define IVANNA_LATTICE_HAS_NEON 1
#else
#define IVANNA_LATTICE_HAS_NEON 0
#endif

namespace ivanna::vis {

static constexpr int GTL_BANDS = 13;
static constexpr int GTL_ORDER = 4; // 4 secciones lattice en cascada, igual orden que v1

static inline float erbBandwidthLattice(float fc) noexcept {
    return 24.7f * (4.37e-3f * fc + 1.0f); // Glasberg & Moore 1990, idéntico a v1
}

struct GammatoneLatticeChannel {
    float fc = 1000.f;
    float gain = 1.f;

    // Por sección: coeficientes de rotación r·cos(w0), r·sin(w0)
    std::array<float, GTL_ORDER> rc{}, rs{};
    // Estado por sección: y1 (salida real), y2 (cuadratura)
    std::array<float, GTL_ORDER> y1{}, y2{};

    void design(float centerFreqHz, float fs) noexcept {
        fc = centerFreqHz;
        const float bw = erbBandwidthLattice(fc);
        const float T  = 1.0f / fs;
        const float b  = 2.0f * (float)M_PI * bw;
        const float w0 = 2.0f * (float)M_PI * fc * T;

        const float r = expf(-b * T); // radio del polo, común a las GTL_ORDER secciones

        for (int sec = 0; sec < GTL_ORDER; ++sec) {
            rc[sec] = r * cosf(w0);
            rs[sec] = r * sinf(w0);
        }

        // Ganancia de normalización: misma derivación analítica que v1
        // (magnitud de sección en resonancia, elevada a -GTL_ORDER).
        const float denomRe = 1.0f - 2.0f * r * cosf(w0) * cosf(w0) + r * r * cosf(2.0f * w0);
        const float denomIm =      - 2.0f * r * cosf(w0) * sinf(w0) + r * r * sinf(2.0f * w0);
        const float magSection = 1.0f / sqrtf(denomRe * denomRe + denomIm * denomIm);
        gain = powf(magSection, -static_cast<float>(GTL_ORDER));
    }

    inline float processBlockEnergy(const float* __restrict__ in, int n) noexcept {
        float acc = 0.f;
        for (int i = 0; i < n; ++i) {
            float x = in[i] * gain;
            for (int sec = 0; sec < GTL_ORDER; ++sec) {
                const float prevY1 = y1[sec];
                const float prevY2 = y2[sec];
                const float nextY1 = rc[sec] * prevY1 - rs[sec] * prevY2 + x;
                const float nextY2 = rs[sec] * prevY1 + rc[sec] * prevY2;
                y1[sec] = nextY1;
                y2[sec] = nextY2;
                x = nextY1;
            }
            acc += x * x;
        }
        return sqrtf(acc / static_cast<float>(n));
    }
};

class GammatoneLattice13 {
public:
    void init(float fs) noexcept {
        fs_ = fs;
        constexpr float fLow = 80.f, fHigh = 16000.f;
        const float erbLow  = hzToErbRate(fLow);
        const float erbHigh = hzToErbRate(fHigh);
        for (int b = 0; b < GTL_BANDS; ++b) {
            const float t = static_cast<float>(b) / (GTL_BANDS - 1);
            const float erb = erbLow + t * (erbHigh - erbLow);
            channels_[b].design(erbRateToHz(erb), fs_);
        }
    }

    // Salida: 13 valores de energía RMS por banda, sin colapsar a bass/mid/high.
    inline void process(const float* __restrict__ in, int n, float out[GTL_BANDS]) noexcept {
        int b = 0;
#if IVANNA_LATTICE_HAS_NEON
        for (; b + 3 < GTL_BANDS; b += 4) {
            process4BandsNeon(b, in, n, out);
        }
#endif
        for (; b < GTL_BANDS; ++b) {
            out[b] = channels_[b].processBlockEnergy(in, n);
        }
    }

#if IVANNA_LATTICE_HAS_NEON
    inline void process4BandsNeon(int base, const float* __restrict__ in, int n, float out[GTL_BANDS]) noexcept {
        float gainV[4], rcV[GTL_ORDER][4], rsV[GTL_ORDER][4];
        float y1V[GTL_ORDER][4], y2V[GTL_ORDER][4];
        for (int lane = 0; lane < 4; ++lane) {
            auto& ch = channels_[base + lane];
            gainV[lane] = ch.gain;
            for (int sec = 0; sec < GTL_ORDER; ++sec) {
                rcV[sec][lane] = ch.rc[sec];
                rsV[sec][lane] = ch.rs[sec];
                y1V[sec][lane] = ch.y1[sec];
                y2V[sec][lane] = ch.y2[sec];
            }
        }

        const float32x4_t gain = vld1q_f32(gainV);
        float32x4_t rc[GTL_ORDER], rs[GTL_ORDER], y1[GTL_ORDER], y2[GTL_ORDER];
        for (int sec = 0; sec < GTL_ORDER; ++sec) {
            rc[sec] = vld1q_f32(rcV[sec]);
            rs[sec] = vld1q_f32(rsV[sec]);
            y1[sec] = vld1q_f32(y1V[sec]);
            y2[sec] = vld1q_f32(y2V[sec]);
        }

        float32x4_t acc = vdupq_n_f32(0.0f);
        for (int i = 0; i < n; ++i) {
            float32x4_t x = vmulq_f32(vdupq_n_f32(in[i]), gain);
            for (int sec = 0; sec < GTL_ORDER; ++sec) {
                const float32x4_t prevY1 = y1[sec];
                const float32x4_t prevY2 = y2[sec];
                // nextY1 = rc*prevY1 - rs*prevY2 + x
                float32x4_t nextY1 = vmlsq_f32(vmlaq_f32(x, rc[sec], prevY1), rs[sec], prevY2);
                // nextY2 = rs*prevY1 + rc*prevY2
                float32x4_t nextY2 = vmlaq_f32(vmulq_f32(rc[sec], prevY2), rs[sec], prevY1);
                y1[sec] = nextY1;
                y2[sec] = nextY2;
                x = nextY1;
            }
            acc = vmlaq_f32(acc, x, x);
        }

        float accV[4];
        vst1q_f32(accV, acc);
        for (int lane = 0; lane < 4; ++lane) {
            out[base + lane] = sqrtf(accV[lane] / static_cast<float>(n));
        }
        for (int sec = 0; sec < GTL_ORDER; ++sec) {
            vst1q_f32(y1V[sec], y1[sec]);
            vst1q_f32(y2V[sec], y2[sec]);
        }
        for (int lane = 0; lane < 4; ++lane) {
            auto& ch = channels_[base + lane];
            for (int sec = 0; sec < GTL_ORDER; ++sec) {
                ch.y1[sec] = y1V[sec][lane];
                ch.y2[sec] = y2V[sec][lane];
            }
        }
    }
#endif

private:
    static float hzToErbRate(float f) noexcept {
        return 21.4f * log10f(4.37e-3f * f + 1.0f);
    }
    static float erbRateToHz(float e) noexcept {
        return (powf(10.f, e / 21.4f) - 1.0f) / 4.37e-3f;
    }

    float fs_ = 48000.f;
    std::array<GammatoneLatticeChannel, GTL_BANDS> channels_;
};

} // namespace ivanna::vis
