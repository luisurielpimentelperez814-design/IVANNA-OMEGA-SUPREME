#pragma once
/*
 * ============================================================
 * IVANNA — PunchEngine: impacto predictivo por bandas (v2.5.41)
 *
 * La dimensión TIEMPO del campo acústico: el ataque de un bombo, una caja o un golpe de bajo es lo que el cuerpo
 * percibe como «empuje». Este motor lo realza SIN subir el nivel medio ni inventar headroom:
 *
 *   1) Separa en 3 bandas con Linkwitz-Riley de 4.º orden (suma plana de magnitud, compensada en fase):
 *        bajo < 160 Hz  |  medio 160 Hz – 2.5 kHz  |  alto > 2.5 kHz
 *   2) Detecta el ataque en cada banda comparando un seguidor de envolvente rápido con uno lento (mismo método
 *      verificable que spatial/TransientDetector.hpp; no es IA) sobre la señal SIN retardo, y aplica la ganancia a
 *      la señal RETARDADA 1.5 ms (lookahead): el realce llega justo cuando empieza el golpe, no después.
 *   3) Ganancia por banda = sostén recortado (hasta -1.4 dB) -> ataque realzado (hasta +5.6/+3.5/+2.6 dB):
 *      sube la relación ataque/sostén (factor de cresta) manteniendo el nivel de pico, como un transient designer.
 *   4) PROTECCIONES (no negociables, sobre cualquier ajuste del usuario):
 *        - guarda de excursión: la banda de graves nunca se realza por encima de ~-6 dBFS de pico (±1 dB: la
 *          suma con las otras bandas en el cruce puede rebasarlo ligeramente);
 *        - guarda de headroom: ningún realce lleva el pico retardado por encima de -0.9 dBFS (si el máster ya
 *          está al techo, el ataque no se sube: solo se recorta el sostén) -> el limitador posterior no «bombea».
 *   5) El enlace L/R es único (la ganancia sale de la envolvente conjunta): la imagen estéreo no se mueve.
 *
 * amount = 0 -> bypass BIT-EXACTO y latencia 0. Con amount > 0 la latencia es lookahead (1.5 ms) y entra/sale con
 * un fundido de 15 ms (sin clics).
 *
 * RT-SAFETY: sin malloc (anillos y filtros fijos), sin locks, sin I/O, O(n) por bloque. setAmount() es atómico:
 * puede llamarse desde cualquier hilo; process() solo desde el hilo de audio.
 * ============================================================
 */
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ivanna {

class PunchEngine {
public:
    static constexpr int   kBands        = 3;
    static constexpr int   kMaxDelay     = 512;           // muestras; soporta lookahead hasta ~10 ms @ 48 kHz
    static constexpr float kLookaheadMs  = 1.5f;
    static constexpr float kXover1Hz     = 160.0f;
    static constexpr float kXover2Hz     = 2500.0f;
    static constexpr float kLowCeiling   = 0.5f;          // -6 dBFS: tope de pico de la banda de graves tras el realce
    static constexpr float kHeadroom     = 0.9f;          // -0.9 dBFS: ningún realce supera este pico retardado
    static constexpr float kSustainTrim  = 0.15f;         // sostén -1.4 dB a amount = 1
    static constexpr float kImpactBoost  = 0.45f;         // el sesgo de impacto escala el realce de ataque hasta x1.45
    static constexpr float kBoost[kBands] = {0.90f, 0.50f, 0.35f};   // ataque: +5.6 / +3.5 / +2.6 dB a amount = 1

    PunchEngine() noexcept { prepare(48000.0f); }

    void prepare(float sampleRate) noexcept {
        sr_ = (std::isfinite(sampleRate) && sampleRate >= 8000.0f && sampleRate <= 768000.0f) ? sampleRate : 48000.0f;
        delay_ = std::clamp(static_cast<int>(std::lround(kLookaheadMs * 0.001f * sr_)), 1, kMaxDelay - 1);
        const float f1 = std::min(kXover1Hz, 0.20f * sr_);
        const float f2 = std::min(kXover2Hz, 0.40f * sr_);
        for (int c = 0; c < 2; ++c) {
            lp1_[c][0].lowpass(f1, sr_);  lp1_[c][1].lowpass(f1, sr_);
            hp1_[c][0].highpass(f1, sr_); hp1_[c][1].highpass(f1, sr_);
            lp2_[c][0].lowpass(f2, sr_);  lp2_[c][1].lowpass(f2, sr_);
            hp2_[c][0].highpass(f2, sr_); hp2_[c][1].highpass(f2, sr_);
            ap2lp_[c][0].lowpass(f2, sr_);  ap2lp_[c][1].lowpass(f2, sr_);       // allpass LR4 en f2 para la banda baja
            ap2hp_[c][0].highpass(f2, sr_); ap2hp_[c][1].highpass(f2, sr_);
        }
        // Envolventes: AMBAS son seguidores de pico asimétricos (mismo tipo de medida), así un tono estable da
        // ratio ~1 y solo un ataque real separa rápido de lento. (La primera versión comparaba un seguidor de pico
        // con una media rectificada: un seno estable daba 1.57 y se confundía con un ataque.)
        //   rápido: ataque 0.4 ms, release por banda 15 / 6 / 3 ms   |   lento: ataque 15 ms, release 150 ms
        const float relMs[kBands] = {15.0f, 6.0f, 3.0f};
        for (int b = 0; b < kBands; ++b) relFast_[b] = std::exp(-1.0f / (relMs[b] * 0.001f * sr_));
        alphaFastAtt_ = 1.0f - std::exp(-1.0f / (0.0004f * sr_));
        alphaSlowAtt_ = 1.0f - std::exp(-1.0f / (0.015f * sr_));
        relSlow_      = std::exp(-1.0f / (0.150f * sr_));
        gAttack_    = 1.0f - std::exp(-1.0f / (0.0002f * sr_));     // 0.2 ms: sigue al ataque
        gRelease_   = 1.0f - std::exp(-1.0f / (0.050f * sr_));      // 50 ms: vuelve suave
        capAttack_  = 1.0f - std::exp(-1.0f / (0.0001f * sr_));     // la guarda baja casi al instante (0.1 ms)
        capRelease_ = gRelease_;
        mixAlpha_   = 1.0f - std::exp(-1.0f / (0.015f * sr_));      // fundido de entrada/salida 15 ms
        peakRel_    = std::exp(-1.0f / (0.020f * sr_));
        lowPeakRel_ = std::exp(-1.0f / (0.030f * sr_));
        reset();
    }

    void reset() noexcept {
        for (int c = 0; c < 2; ++c) {
            for (int s = 0; s < 2; ++s) {
                lp1_[c][s].clear(); hp1_[c][s].clear(); lp2_[c][s].clear(); hp2_[c][s].clear();
                ap2lp_[c][s].clear(); ap2hp_[c][s].clear();
            }
            for (int b = 0; b < kBands; ++b) ring_[c][b].fill(0.0f);
        }
        wr_ = 0;
        for (int b = 0; b < kBands; ++b) { envFast_[b] = envSlow_[b] = 0.0f; gain_[b] = 1.0f; gCap_[b] = kNoCap; }
        peakDelayed_ = lowPeak_ = 0.0f;
        mix_ = 0.0f;
        amountSmooth_ = 0.0f;
        for (auto& t : tel_) t.store(0.0f, std::memory_order_relaxed);
    }

    // Sesgo de IMPACTO 0..1 (lo fija el arco narrativo 4D en el regreso del pulso tras un hueco, el «drop»): sube hasta
    // un 45 % el realce de ataque MIENTRAS dura. No toca el sostén ni las guardas: excursión y headroom siguen
    // mandando, así que nunca produce más pico del permitido. 0 = comportamiento idéntico al de siempre.
    void setImpactBias(float b) noexcept {
        if (std::isfinite(b)) impactBias_.store(std::clamp(b, 0.0f, 1.0f), std::memory_order_relaxed);
    }
    float impactBias() const noexcept { return impactBias_.load(std::memory_order_relaxed); }

    // Cambia la frecuencia de muestreo (reinicia filtros y anillos solo si de verdad cambió).
    void setSampleRate(float sampleRate) noexcept {
        if (std::isfinite(sampleRate) && sampleRate >= 8000.0f && sampleRate <= 768000.0f && sampleRate != sr_) prepare(sampleRate);
    }

    // 0 = apagado (bit-exacto), 1 = máximo. No finito se ignora.
    void setAmount(float a) noexcept {
        if (std::isfinite(a)) amountTarget_.store(std::clamp(a, 0.0f, 1.0f), std::memory_order_relaxed);
    }
    float amount() const noexcept { return amountTarget_.load(std::memory_order_relaxed); }
    // Retardo que añade el motor al camino cuando está activo (0 con amount = 0 y fundido terminado).
    int latencySamples() const noexcept { return (mix_ > 0.0f || amountTarget_.load(std::memory_order_relaxed) > 0.0f) ? delay_ : 0; }
    float lookaheadMs() const noexcept { return 1000.0f * static_cast<float>(delay_) / sr_; }

    // Telemetría (cualquier hilo): [0..2] ganancia dB por banda (bajo/medio/alto), [3] actividad de ataque 0..1,
    // [4] guarda de excursión/headroom activa 0/1, [5] latencia en ms (0 si está apagado).
    static constexpr int kTelemetrySize = 6;
    void telemetry(float* out) const noexcept {
        if (!out) return;
        for (int i = 0; i < kTelemetrySize; ++i) out[i] = tel_[i].load(std::memory_order_relaxed);
    }

    void process(float* L, float* R, int n) noexcept {
        if (!L || !R || n <= 0) return;
        const float target = amountTarget_.load(std::memory_order_relaxed);
        const float impactScale = 1.0f + kImpactBoost * impactBias_.load(std::memory_order_relaxed);
        if (target <= 0.0f && mix_ <= 0.0f && amountSmooth_ <= 0.0f) {          // apagado y asentado: bit-exacto
            publishIdle();
            return;
        }
        float maxG[kBands] = {1.0f, 1.0f, 1.0f};
        float minG[kBands] = {1.0f, 1.0f, 1.0f};
        float transSum = 0.0f;
        bool guard = false;

        for (int i = 0; i < n; ++i) {
            const float xl = finite(L[i]), xr = finite(R[i]);
            amountSmooth_ += mixAlpha_ * (target - amountSmooth_);
            mix_          += mixAlpha_ * ((target > 0.0f ? 1.0f : 0.0f) - mix_);
            // Al apagar, los suavizados decaen exponencialmente y tardarían ~1.5 s en llegar a 0 (pasando por
            // denormales, que cuestan CPU en ARM): se fijan a 0 por debajo de -80 dB para volver al bypass exacto.
            if (target <= 0.0f) {
                if (amountSmooth_ < 1.0e-4f) amountSmooth_ = 0.0f;
                if (mix_ < 1.0e-4f) mix_ = 0.0f;
            }
            const float amt = amountSmooth_;

            // 1) split LR4 de 3 bandas por canal (sobre la señal SIN retardo)
            float band[2][kBands];
            split(0, xl, band[0]);
            split(1, xr, band[1]);

            // 2) escribir en anillos (retardo) y leer la muestra retardada
            for (int c = 0; c < 2; ++c) for (int b = 0; b < kBands; ++b) ring_[c][b][wr_] = band[c][b];
            const int rd = (wr_ - delay_ + kMaxDelay) % kMaxDelay;
            float dl[kBands], dr[kBands];
            for (int b = 0; b < kBands; ++b) { dl[b] = ring_[0][b][rd]; dr[b] = ring_[1][b][rd]; }
            wr_ = (wr_ + 1) % kMaxDelay;

            // 3) pico del original retardado (guarda de headroom) y pico de la banda grave retardada (excursión)
            const float inPeak = std::max(std::fabs(dl[0] + dl[1] + dl[2]), std::fabs(dr[0] + dr[1] + dr[2]));
            peakDelayed_ = std::max(inPeak, peakDelayed_ * peakRel_);
            const float lowAbs = std::max(std::fabs(dl[0]), std::fabs(dr[0]));
            lowPeak_ = std::max(lowAbs, lowPeak_ * lowPeakRel_);

            float outL = 0.0f, outR = 0.0f;
            for (int b = 0; b < kBands; ++b) {
                // 4) detección de ataque sobre la envolvente conjunta SIN retardo
                const float a = 0.5f * (std::fabs(band[0][b]) + std::fabs(band[1][b]));
                envFast_[b] = (a > envFast_[b]) ? envFast_[b] + (a - envFast_[b]) * alphaFastAtt_ : envFast_[b] * relFast_[b];
                envSlow_[b] = (a > envSlow_[b]) ? envSlow_[b] + (a - envSlow_[b]) * alphaSlowAtt_ : envSlow_[b] * relSlow_;
                float trans = 0.0f;
                if (envFast_[b] > 1.0e-4f) {
                    const float ratio = envFast_[b] / std::max(envSlow_[b], 1.0e-6f);
                    trans = std::clamp((ratio - 1.5f) / (3.5f - 1.5f), 0.0f, 1.0f);
                }
                // 5) ganancia deseada: sostén recortado -> ataque realzado (suavizada: ataque 0.2 ms, release 50 ms)
                const float sustain = 1.0f - kSustainTrim * amt;
                const float attack  = 1.0f + kBoost[b] * amt * impactScale;
                const float gTarget = sustain + (attack - sustain) * trans;
                gain_[b] += ((gTarget > gain_[b]) ? gAttack_ : gRelease_) * (gTarget - gain_[b]);
                // 6) GUARDAS como tope SUAVIZADO independiente (baja en 0.1 ms, sube en 50 ms): el tope sale del pico
                //    de la señal RETARDADA que se va a reproducir, así que actúa justo cuando llega el golpe aunque la
                //    ganancia «predictiva» ya estuviera arriba. Nunca por debajo del recorte de sostén.
                float cap = kHeadroom / std::max(peakDelayed_, 1.0e-6f);                           // headroom
                if (b == 0) cap = std::min(cap, kLowCeiling / std::max(lowPeak_, 1.0e-6f));        // excursión de graves
                const float capTarget = std::min(std::max(sustain, cap), kNoCap);
                gCap_[b] += ((capTarget < gCap_[b]) ? capAttack_ : capRelease_) * (capTarget - gCap_[b]);
                const float g = std::min(gain_[b], gCap_[b]);
                if (gCap_[b] < gain_[b] - 0.01f && trans > 0.05f && amt > 0.05f) guard = true;
                maxG[b] = std::max(maxG[b], g);
                minG[b] = std::min(minG[b], g);
                transSum += trans;
                outL += dl[b] * g;
                outR += dr[b] * g;
            }
            // 6) fundido entre el camino directo (sin retardo) y el procesado (retardado 1.5 ms)
            const float m = mix_;
            L[i] = (1.0f - m) * xl + m * outL;
            R[i] = (1.0f - m) * xr + m * outR;
        }

        const float inv = 1.0f / static_cast<float>(n * kBands);
        for (int b = 0; b < kBands; ++b) {
            const float g = (maxG[b] - 1.0f > 1.0f - minG[b]) ? maxG[b] : minG[b];      // el extremo dominante del bloque
            tel_[b].store(20.0f * std::log10(std::max(g, 1.0e-3f)), std::memory_order_relaxed);
        }
        tel_[3].store(std::clamp(transSum * inv, 0.0f, 1.0f), std::memory_order_relaxed);
        tel_[4].store(guard ? 1.0f : 0.0f, std::memory_order_relaxed);
        tel_[5].store(1000.0f * static_cast<float>(delay_) / sr_ * mix_, std::memory_order_relaxed);
    }

private:
    struct Biquad {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void clear() noexcept { z1 = z2 = 0.0f; }
        void lowpass(float fc, float sr) noexcept { design(fc, sr, false); }
        void highpass(float fc, float sr) noexcept { design(fc, sr, true); }
        void design(float fc, float sr, bool hp) noexcept {                 // Butterworth 2.º orden (Q = 1/sqrt 2)
            const double w0 = 2.0 * 3.14159265358979323846 * static_cast<double>(fc) / static_cast<double>(sr);
            const double cw = std::cos(w0), alpha = std::sin(w0) / (2.0 * 0.70710678118654752);
            const double a0 = 1.0 + alpha;
            const double n0 = hp ? (1.0 + cw) * 0.5 : (1.0 - cw) * 0.5;
            b0 = static_cast<float>(n0 / a0);
            b1 = static_cast<float>((hp ? -(1.0 + cw) : (1.0 - cw)) / a0);
            b2 = b0;
            a1 = static_cast<float>(-2.0 * cw / a0);
            a2 = static_cast<float>((1.0 - alpha) / a0);
        }
        float run(float x) noexcept {                                       // forma traspuesta II
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    static float finite(float v) noexcept { return std::isfinite(v) ? v : 0.0f; }

    // LR4 = dos Butterworth en cascada. low = LP1; rest = HP1; mid = LP2(rest); high = HP2(rest).
    // La banda baja pasa por un allpass LR4 en f2 (LP2+HP2) para igualar la fase del camino alto: suma plana.
    void split(int c, float x, float* out) noexcept {
        const float lo   = lp1_[c][1].run(lp1_[c][0].run(x));
        const float rest = hp1_[c][1].run(hp1_[c][0].run(x));
        const float mid  = lp2_[c][1].run(lp2_[c][0].run(rest));
        const float high = hp2_[c][1].run(hp2_[c][0].run(rest));
        out[0] = ap2lp_[c][1].run(ap2lp_[c][0].run(lo)) + ap2hp_[c][1].run(ap2hp_[c][0].run(lo));
        out[1] = mid;
        out[2] = high;
    }

    void publishIdle() noexcept {
        for (auto& t : tel_) t.store(0.0f, std::memory_order_relaxed);
    }

    float sr_ = 48000.0f;
    int   delay_ = 72;
    std::array<std::array<Biquad, 2>, 2> lp1_{}, hp1_{}, lp2_{}, hp2_{}, ap2lp_{}, ap2hp_{};
    std::array<std::array<std::array<float, kMaxDelay>, kBands>, 2> ring_{};
    int   wr_ = 0;
    static constexpr float kNoCap = 100.0f;
    float envFast_[kBands]{}, envSlow_[kBands]{}, gain_[kBands]{1.0f, 1.0f, 1.0f}, relFast_[kBands]{};
    float gCap_[kBands]{kNoCap, kNoCap, kNoCap};
    float alphaFastAtt_ = 0.0f, alphaSlowAtt_ = 0.0f, relSlow_ = 0.0f, capAttack_ = 0.0f, capRelease_ = 0.0f;
    float gAttack_ = 0.0f, gRelease_ = 0.0f, mixAlpha_ = 0.0f, peakRel_ = 0.0f, lowPeakRel_ = 0.0f;
    float peakDelayed_ = 0.0f, lowPeak_ = 0.0f;
    float mix_ = 0.0f, amountSmooth_ = 0.0f;
    std::atomic<float> amountTarget_{0.0f};
    std::atomic<float> impactBias_{0.0f};
    std::array<std::atomic<float>, kTelemetrySize> tel_{};
};

} // namespace ivanna
