#include "IntelligentUpmixer.hpp"
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Ivanna {

namespace {
constexpr float kCrossoverHz = 140.0f;   // graves por debajo → centro (mono-seguro)
constexpr float kSmoothMs    = 15.0f;    // suavizado de inmersividad / morphing
constexpr float kTransRecoverMs = 20.0f; // recuperación del ancho tras un transiente

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
    widthMorph_ = targetImmersivity_;
    transientWidth_ = 1.0f;
}

void IntelligentUpmixer::setImmersivity(float value) noexcept {
    targetImmersivity_ = clamp01(value);
}

void IntelligentUpmixer::processBlock(const float* inL, const float* inR,
                                      std::vector<HoaVector>& outField,
                                      std::size_t numFrames) noexcept {
    // Reserva una sola vez: resize() repetido en el hot path puede realojar
    // el vector si numFrames varía entre bloques; reserve() al tamaño máximo
    // visto elimina toda realocación posterior.
    if (outField.capacity() < numFrames) outField.reserve(numFrames);
    if (outField.size() != numFrames) outField.resize(numFrames);
    if (numFrames == 0 || inL == nullptr || inR == nullptr) return;

    // Bases con ENERGÍA UNITARIA exacta (encodeUnitPower): la energía del campo
    // no depende del azimut ni de cuántas fuentes se mezclen — la inmersividad
    // cambia la ANCHURA, nunca el NIVEL.
    // REFINAMIENTO (2026-09-17): las 5 bases de codificación son constantes
    // del sistema — antes se recalculaban en CADA bloque de audio (5
    // evaluaciones de encodeUnitPower por llamada a processBlock). Ahora se
    // evalúan una sola vez por proceso (static const, inicialización
    // thread-safe garantizada por C++11) y el hot path solo las lee.
    static const HoaVector encNarrowL = HoaGainMatrix::encodeUnitPower( static_cast<float>(M_PI) / 6.0f); // +30°
    static const HoaVector encNarrowR = HoaGainMatrix::encodeUnitPower(-static_cast<float>(M_PI) / 6.0f); // -30°
    static const HoaVector encWideL   = HoaGainMatrix::encodeUnitPower( static_cast<float>(M_PI) / 2.0f); // +90°
    static const HoaVector encWideR   = HoaGainMatrix::encodeUnitPower(-static_cast<float>(M_PI) / 2.0f); // -90°
    static const HoaVector encCenter  = HoaGainMatrix::encodeUnitPower(0.0f);                              // 0°

    // FIX (eco/desface al activar el toggle o subir el slider, reporte del
    // propietario 2026-09-17): la entrada/salida del upmixing era un bypass
    // duro — al activarlo, la señal saltaba de la ruta directa a la ruta
    // HOA+HRTF (que añade su propia latencia FIR) de golpe: la cola del
    // filtro y el cambio de fase se percibian como un eco breve y un
    // desface entre canales. Ahora la entrada y la salida se hacen con un
    // crossfade por muestra (m_blockMix_, ~15 ms) entre la señal seca y la
    // decodificada: ambas rutas llegan al mismo instante durante la
    // transición — sin eco, sin desface, sin doble ruta simultánea.
    if (!enabled_ || smoothedImmersivity_ <= 0.001f) {
        // Bypass transparente: par estéreo EXACTO a ±30° con energía unitaria.
        for (std::size_t i = 0; i < numFrames; ++i) {
            HoaVector out = {0};
            HoaGainMatrix::accumulate(out, encNarrowL, sanitize(inL[i]));
            HoaGainMatrix::accumulate(out, encNarrowR, sanitize(inR[i]));
            outField[i] = out;
        }
        return;
    }

    // Detección de transientes sobre el pico estéreo max(|L|,|R|) — no sobre el
    // mid (que cancela golpes paneados) ni sobre un solo canal. El resultado SÍ
    // se usa: en el ataque se estrecha el ancho (transiente localizado al
    // frente) y se recupera en rampa de ~20 ms.
    if (monoBuf_.size() != numFrames) monoBuf_.resize(numFrames);
    for (std::size_t i = 0; i < numFrames; ++i) {
        const float a = std::fabs(sanitize(inL[i]));
        const float b = std::fabs(sanitize(inR[i]));
        monoBuf_[i] = a > b ? a : b;
    }
    const bool hasTransients = transientDetector_.processBlock(monoBuf_.data(), numFrames);
    const float transTarget  = hasTransients ? 0.55f : 1.0f; // estrechar en el ataque
    const float transA       = 1.0f - std::exp(-1.0f / (sampleRate_ * kTransRecoverMs * 0.001f));

    for (std::size_t i = 0; i < numFrames; ++i) {
        const float l = sanitize(inL[i]);
        const float r = sanitize(inR[i]);

        const float mid  = 0.5f * (l + r);
        const float side = 0.5f * (l - r);

        // Crossover complementario de 2º orden sobre el mid: bass + midHi == mid
        // EXACTO en cada muestra (suma constante, sin error de fase en el corte).
        bassZ1_ += lpfA_ * (mid - bassZ1_);
        bassZ2_ += lpfA_ * (bassZ1_ - bassZ2_);
        const float bass  = bassZ2_;
        const float midHi = mid - bass;

        // Suavizado por muestra de inmersividad y morphing (sin zipper).
        smoothedImmersivity_ += smoothA_ * (targetImmersivity_ - smoothedImmersivity_);
        widthMorph_          += smoothA_ * (smoothedImmersivity_ - widthMorph_);
        transientWidth_      += transA  * (transTarget - transientWidth_);

        const float w = smoothedImmersivity_;
        // Morfología de bases: el par directo se abre de ±30° a ±90° según la
        // inmersividad, con energía de campo plana (bases unitarias).
        const float m = widthMorph_ * transientWidth_;
        const float gN = 1.0f - 0.5f * m;   // peso del par estrecho
        const float gW = 0.5f * m;          // peso del par ancho

        HoaVector out = {0};
        // 1) Par estéreo directo (estrecho ↔ ancho, energía plana).
        HoaGainMatrix::accumulate(out, encNarrowL, l * gN);
        HoaGainMatrix::accumulate(out, encNarrowR, r * gN);
        HoaGainMatrix::accumulate(out, encWideL,   l * gW);
        HoaGainMatrix::accumulate(out, encWideR,   r * gW);
        // 2) Expansión inmersiva: presencia central + graves mono-seguros al
        //    centro + apertura lateral difusa.
        HoaGainMatrix::accumulate(out, encCenter, midHi * w);
        HoaGainMatrix::accumulate(out, encCenter, bass  * w);
        HoaGainMatrix::accumulate(out, encWideL,  side  * w * 0.5f);
        HoaGainMatrix::accumulate(out, encWideR, -side  * w * 0.5f);

        outField[i] = out;
    }
}

} // namespace Ivanna
