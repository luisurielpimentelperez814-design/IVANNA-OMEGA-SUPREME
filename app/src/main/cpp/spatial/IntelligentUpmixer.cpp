#include "IntelligentUpmixer.hpp"
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Ivanna {

namespace {
// Corte del crossover: 140 Hz. Por debajo, el contenido se mantiene al centro
// (mono-seguro): un grave abierto lateralmente se cancela en mono y suena
// "hueco" en altavoces/auriculares con poca separación.
constexpr float kCrossoverHz = 140.0f;
constexpr float kSmoothMs    = 15.0f;   // constante de suavizado de inmersividad

inline float sanitize(float v) noexcept { return std::isfinite(v) ? v : 0.0f; }
inline float clamp01(float v) noexcept {
    if (!std::isfinite(v)) return 0.0f;
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}
} // namespace

void IntelligentUpmixer::prepare(float sampleRate) noexcept {
    sampleRate_ = (std::isfinite(sampleRate) && sampleRate > 0.0f) ? sampleRate : 48000.0f;

    transientDetector_.prepare(sampleRate_);
    transientDetector_.setThreshold(3.0f);

    const float dt = 1.0f / sampleRate_;
    const float RC = 1.0f / (2.0f * static_cast<float>(M_PI) * kCrossoverHz);
    lpfA_    = dt / (RC + dt);
    smoothA_ = 1.0f - std::exp(-dt / (kSmoothMs * 0.001f));

    bassZ1_ = bassZ2_ = 0.0f;
    smoothedImmersivity_ = targetImmersivity_;
}

void IntelligentUpmixer::setImmersivity(float value) noexcept {
    targetImmersivity_ = clamp01(value);
}

void IntelligentUpmixer::processBlock(const float* inL, const float* inR,
                                      std::vector<HoaVector>& outField,
                                      std::size_t numFrames) noexcept {
    if (outField.size() != numFrames) outField.resize(numFrames);
    if (numFrames == 0 || inL == nullptr || inR == nullptr) return;

    // Vectores de codificación HOA — una vez por bloque, no por muestra.
    const HoaVector encL    = HoaGainMatrix::encode(static_cast<float>(M_PI) / 6.0f);  // +30°
    const HoaVector encR    = HoaGainMatrix::encode(-static_cast<float>(M_PI) / 6.0f); // -30°
    const HoaVector encMid  = HoaGainMatrix::encode(0.0f);
    const HoaVector encSide = HoaGainMatrix::encode(static_cast<float>(M_PI) / 2.0f);

    if (!enabled_ || smoothedImmersivity_ <= 0.001f) {
        // Bypass transparente: par estéreo EXACTO a ±30° — la imagen original
        // se reconstruye sin pérdida (misma codificación, ganancia unitaria).
        for (std::size_t i = 0; i < numFrames; ++i) {
            HoaVector out = {0};
            HoaGainMatrix::accumulate(out, encL, sanitize(inL[i]));
            HoaGainMatrix::accumulate(out, encR, sanitize(inR[i]));
            outField[i] = out;
        }
        return;
    }

    // Detección de transientes sobre el MONO real (mid) — corrige el sesgo de
    // alimentar sólo un canal y por fin USA el resultado (antes era código muerto).
    if (monoBuf_.size() != numFrames) monoBuf_.resize(numFrames);
    for (std::size_t i = 0; i < numFrames; ++i) {
        monoBuf_[i] = 0.5f * (sanitize(inL[i]) + sanitize(inR[i]));
    }
    const bool  hasTransients = transientDetector_.processBlock(monoBuf_.data(), numFrames);
    const float widthScale     = hasTransients ? 1.35f : 1.0f;

    for (std::size_t i = 0; i < numFrames; ++i) {
        const float l = sanitize(inL[i]);
        const float r = sanitize(inR[i]);

        const float mid  = 0.5f * (l + r);
        const float side = 0.5f * (l - r);

        // Crossover complementario de 2º orden sobre el mid (dos polos en
        // cascada, 12 dB/oct). bass + midHi == mid EXACTO: cruce de suma
        // constante, sin error de fase en el corte ni overshoot.
        bassZ1_ += lpfA_ * (mid - bassZ1_);
        bassZ2_ += lpfA_ * (bassZ1_ - bassZ2_);
        const float bass  = bassZ2_;
        const float midHi = mid - bass;

        // Suavizado por muestra de la inmersividad (sin zipper al mover el control).
        smoothedImmersivity_ += smoothA_ * (targetImmersivity_ - smoothedImmersivity_);
        const float w  = smoothedImmersivity_;
        const float dg = 1.0f - 0.5f * w;   // el par estéreo cede sitio a la expansión

        HoaVector out = {0};
        // 1) Imagen estéreo exacta (±30°).
        HoaGainMatrix::accumulate(out, encL, l * dg);
        HoaGainMatrix::accumulate(out, encR, r * dg);
        // 2) Expansión inmersiva: presencia central + graves mono-seguros al
        //    centro + apertura lateral difusa (con realce en transientes).
        HoaGainMatrix::accumulate(out, encMid,  midHi * w);
        HoaGainMatrix::accumulate(out, encMid,  bass  * w);
        HoaGainMatrix::accumulate(out, encSide, side  * w * widthScale);

        outField[i] = out;
    }
}

} // namespace Ivanna
