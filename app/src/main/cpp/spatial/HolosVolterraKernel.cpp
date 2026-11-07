// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#include "HolosVolterraKernel.hpp"

#include <algorithm>
#include <cstring>

#include "../neuromorphic/volterra_h2_symmetric.hpp"
#include "../neuromorphic/volterra_kernel_package.hpp"

namespace ivanna {
namespace holos {

namespace {
constexpr float kPiF = 3.14159265358979323846f;
}  // namespace

struct HolosVolterraKernel::VolterraImpl {
    ivanna::dsp::VolterraH2Symmetric engine{64, 2};
};

HolosVolterraKernel::HolosVolterraKernel() = default;
HolosVolterraKernel::~HolosVolterraKernel() = default;

void HolosVolterraKernel::prepare(float sr) {
    const float s = std::isfinite(sr) ? std::clamp(sr, 8000.0f, 384000.0f) : 48000.0f;
    sampleRate_ = s;
    fundCoef_ = std::exp(-1.0f / (s * 0.003f));  // EMA 3 ms (regresión, a tasa base)
    for (int c = 0; c < 2; ++c) {
        Channel& ch = ch_[c];
        ch.hpf.setHighpass(3000.0, 0.707, (double)s);
        ch.lpf.setLowpass(8000.0, 0.707, (double)s);
        ch.lpfGold.setLowpass(8000.0, 0.707, (double)s);
        ch.dcR = std::exp(-2.0f * kPiF * 15.0f / s);  // DC-block 15 Hz
        // Envolvente OHC Heun (RK2): ataque 10 ms / release 80 ms, por muestra.
        ch.heunAtt = 1.0f - std::exp(-1.0f / (s * 0.010f));
        ch.heunRel = 1.0f - std::exp(-1.0f / (s * 0.080f));
    }
    if (!volterra_) volterra_ = std::make_unique<VolterraImpl>();
    interleave_.assign(2 * 512, 0.0f);
    prepared_ = true;
    reset();
}

void HolosVolterraKernel::reset() noexcept {
    for (int c = 0; c < 2; ++c) {
        Channel& ch = ch_[c];
        ch.hpf.reset();
        ch.lpf.reset();
        ch.lpfGold.reset();
        ch.dcX = 0.0f;
        ch.dcY = 0.0f;
        ch.h2Mean = 0.0f;
        ch.fundCross = 0.0f;
        ch.fundPow = 0.0f;
        ch.fundGain = 0.0f;
        ch.heunEnv = 0.0f;
    }
}

void HolosVolterraKernel::setParams(const VolterraKernelParams& p) noexcept {
    harmonics_.store(std::isfinite(p.harmonics) ? std::clamp(p.harmonics, 0.0f, 1.0f) : 0.0f,
                     std::memory_order_relaxed);
    warmth_.store(std::isfinite(p.warmth) ? std::clamp(p.warmth, 0.0f, 1.0f) : 0.5f,
                  std::memory_order_relaxed);
    driveNorm_.store(std::isfinite(p.driveNorm) ? std::clamp(p.driveNorm, 0.0f, 1.0f) : 0.45f,
                     std::memory_order_relaxed);
    h3Enabled_.store(p.h3Enabled, std::memory_order_relaxed);
}

bool HolosVolterraKernel::hasVolterraKernel() const noexcept {
    return volterra_ != nullptr && volterra_->engine.hasEffectiveKernel();
}

int HolosVolterraKernel::loadKernelPackage(const uint8_t* package, size_t packageSize) noexcept {
    if (!volterra_ || package == nullptr || packageSize == 0) return -1;
    ivanna::dsp::VolterraLoadPolicy policy;
    policy.expected_sample_rate_hz = (uint32_t)sampleRate_;
    // La puerta IVK1 exige motor deshabilitado + silencioso (EngineBusy si
    // no): deshabilitar antes de cargar. En éxito el wrapper RE-habilita
    // (el cargador en crudo no lo hace): con kernel cargado, el término con
    // memoria de K debe sonar (el envelope hace el fade-in). En error queda
    // en bypass (deshabilitado), como documenta el cargador. Llamar con el
    // motor en silencio (antes de procesar audio, o tras deshabilitar y
    // drenar hasta isSilent()).
    volterra_->engine.setEnabled(false);
    const auto st = volterra_->engine.loadKernelPackage(package, packageSize, policy);
    if (st == ivanna::dsp::VolterraLoadStatus::Ok) volterra_->engine.setEnabled(true);
    return (int)st;
}

float HolosVolterraKernel::heunEnvelope(int ch) const noexcept {
    if (ch < 0 || ch > 1) return 0.0f;
    return ch_[ch].heunEnv;
}

float HolosVolterraKernel::fundGain(int ch) const noexcept {
    if (ch < 0 || ch > 1) return 0.0f;
    return ch_[ch].fundGain;
}

float HolosVolterraKernel::tickChannel(float x, Channel& c) noexcept {
    if (!std::isfinite(x)) return 0.0f;
    const float harm = harmonics_.load(std::memory_order_relaxed);
    const float warmth = warmth_.load(std::memory_order_relaxed);
    const float driveNorm = driveNorm_.load(std::memory_order_relaxed);
    const float driveLin = 1.0f + 3.0f * driveNorm;  // como el Exciter (drive_ = 1+3·d)

    // ── Envolvente OHC Heun (RK2) sobre |x|: gobierna el h2 ──
    const float ax = std::fabs(x);
    const float hc = (ax > c.heunEnv) ? c.heunAtt : c.heunRel;
    const float k1 = (ax - c.heunEnv) * hc;
    const float pred = c.heunEnv + k1;
    const float k2h = (ax - pred) * hc;
    c.heunEnv += 0.5f * (k1 + k2h);
    // nl_b = 0.085·env² + 0.05·env (forma funcional coclear, §1.7):
    // a más nivel, la OHC comprime → menos h2 (k2Scale < 1).
    const float nl = std::clamp(0.085f * c.heunEnv * c.heunEnv + 0.05f * c.heunEnv, 0.0f, 0.45f);
    const float k2Scale = 1.0f - 0.5f * nl;

    // ── Camino Exciter (>3 kHz, <8 kHz) ──
    const float h = c.hpf.process(c.lpf.process(x));
    // Cheb T2/T3 con el mismo polinomio que ChebHarmonicShaper::tick
    // (entrada escalada por driveLin, drive normalizado). H3 off en tier
    // térmico WARM: se anula el término cúbico (warmth→1 hace k3→0.063·dEff;
    // aquí se fuerza k3=0 pasando warmth=1 y escalando después — más limpio:
    // se calcula el polinomio y se resta la parte cúbica medida).
    float cheb;
    if (h3Enabled_.load(std::memory_order_relaxed)) {
        const float y = chebPolyRaw(h * driveLin, driveNorm, warmth, 0.0f, k2Scale);
        // DC-block 15 Hz (misma fórmula que el shaper).
        const float out = y - c.dcX + c.dcR * c.dcY;
        c.dcX = y;
        c.dcY = std::isfinite(out) ? out : 0.0f;
        cheb = c.dcY;
    } else {
        // Solo H2: el polinomio con warmth=1 da k3 = 0.42·0.15·dEff (no
        // cero exacto); se reconstruye H2 puro: y = (1−k2)·u + k2·u².
        const float dEff = driveNorm;
        const float k2 = 0.42f * 1.0f * dEff * k2Scale;
        float uu = h * driveLin;
        const float axu = std::fabs(uu);
        if (axu > 0.85f) {
            const float ex = axu - 0.85f;
            const float sat = 0.85f + 0.15f * (ex / (0.15f + ex));
            uu = (uu >= 0.0f) ? sat : -sat;
        }
        const float y = (1.0f - k2) * uu + k2 * uu * uu;
        const float out = y - c.dcX + c.dcR * c.dcY;
        c.dcX = y;
        c.dcY = std::isfinite(out) ? out : 0.0f;
        cheb = c.dcY;
    }
    // Padé [3/2] residual (misma racional que el Exciter, modo 1: 35%).
    // Es una no-linealidad IMPAR (su serie de Taylor es x + c3·x³ + …):
    // con H3 off (tier WARM) también se anula, o el tier no cumpliría su
    // promesa — el H3 residual del Padé era ~la mitad del H3 total.
    const float pade = h3Enabled_.load(std::memory_order_relaxed)
                             ? (softClipPade(h, driveLin) - h)
                             : 0.0f;
    // ── Camino GoldenEar H2 (<8 kHz): 2·lf² con DC por EMA ──
    const float lf = c.lpfGold.process(x);
    const float rawH2 = 2.0f * lf * lf;
    c.h2Mean += 0.002f * (rawH2 - c.h2Mean);  // τ≈500 muestras ≈10.4 ms
    const float h2gold = rawH2 - c.h2Mean;

    // ── Una sola mezcla, una sola corrección de regresión ──
    float exc = 0.65f * cheb + 0.35f * pade + 0.12f * h2gold;
    {
        c.fundCross = fundCoef_ * c.fundCross + (1.0f - fundCoef_) * (exc * h);
        c.fundPow = fundCoef_ * c.fundPow + (1.0f - fundCoef_) * (h * h);
        if (c.fundPow > 1.0e-8f)
            c.fundGain = std::clamp(c.fundCross / c.fundPow, -8.0f, 8.0f);
        exc -= c.fundGain * h;
    }
    float y = x + harm * exc * kExciterCeiling;
    return std::isfinite(y) ? y : 0.0f;
}

void HolosVolterraKernel::process(float* l, float* r, int n) noexcept {
    if (!prepared_ || l == nullptr || r == nullptr || n <= 0) return;
    const bool hasKernel = hasVolterraKernel();
    const float harm = harmonics_.load(std::memory_order_relaxed);
    // Identidad exacta de coste cero: sin armónicos y sin kernel.
    if (harm <= 0.0f && !hasKernel) return;
    for (int i = 0; i < n; ++i) {
        l[i] = tickChannel(l[i], ch_[0]);
        r[i] = tickChannel(r[i], ch_[1]);
    }
    if (hasKernel) {
        // Memoria Volterra H2 (K=64, triangular): interleave → process → deinterleave.
        // Scratch preasignado (2×512); n mayores se procesan por trozos.
        int off = 0;
        while (off < n) {
            const int m = std::min(n - off, 512);
            for (int i = 0; i < m; ++i) {
                interleave_[2 * i] = l[off + i];
                interleave_[2 * i + 1] = r[off + i];
            }
            volterra_->engine.processInterleaved(interleave_.data(), interleave_.data(),
                                                 (uint32_t)m, 2);
            for (int i = 0; i < m; ++i) {
                l[off + i] = interleave_[2 * i];
                r[off + i] = interleave_[2 * i + 1];
            }
            off += m;
        }
    }
}

}  // namespace holos
}  // namespace ivanna
