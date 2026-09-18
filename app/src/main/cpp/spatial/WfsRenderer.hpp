// WfsRenderer.hpp
// ============================================================================
// IVANNA — Wave Field Synthesis (WFS) Renderer
// ============================================================================
// (c) 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// Síntesis de campo de ondas sobre un array circular de altavoces
// VIRTUALES que luego se renderiza binauralmente (ITD/ILD por altavoz).
// Cada objeto de audio actúa como fuente primaria cuyo frente de onda se
// reconstruye por superposición de las ondas secundarias de cada altavoz
// del array (principio de Huygens):
//
//   driving(o, s) = gain_o * WFS_ATTENUATION(d_os) * focusWeight(o, s)
//   señal_s(t)   = Σ_o  src_o(t − d_os/c) * driving(o, s)
//
// con atenuación 1/sqrt(d) (onda cilíndrica, régimen de campo del array)
// y peso de focalización coseno (la contribución de un altavoz se atenúa
// cuando el objeto queda fuera de su semiespacio frontal — aproximación
// estándar de la driving function de WFS 2.5D).
//
// Coste: O(objetos × altavoces × frames) con delays fraccionarios por
// interpolación lineal — ~25k MACs por bloque de 384 frames con 4
// objetos × 16 altavoces: viable en el audio thread sin SIMD.
//
// Sin dependencias externas: solo <cmath>, <vector>, <array>, <cstdint>.
// ============================================================================
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ivanna::spatial {

class WfsRenderer {
public:
    static constexpr int   kMaxSpeakers   = 32;
    static constexpr float kSpeedOfSound  = 343.0f;    // m/s
    static constexpr float kArrayRadiusM  = 1.5f;      // radio del array virtual
    static constexpr float kRefDistanceM  = 1.0f;      // distancia de referencia 0 dB
    static constexpr float kMaxObjectDist = 8.0f;      // clamp de distancia de fuente
    static constexpr float kMaxItdS       = 0.00063f;  // ITD máximo interaural (~63 cm/c... cabeza)

    bool init(float sampleRate, int blockSize, int numSpeakers = 16) noexcept;
    void reset() noexcept;

    // Posición/actualización de fuente primaria. x/y en metros relativos al
    // oyente (x: derecha+, y: frente+). gain lineal. thread-safety: llamar
    // desde el hilo de control entre bloques (sin locks — el audio thread
    // lee copias consistentes por bloque en process()).
    void setObject(int id, float x, float y, float gain) noexcept;
    void removeObject(int id) noexcept;
    void clearObjects() noexcept { objects_.clear(); }

    // Procesa UN bloque: para cada objeto con entrada en objectInputs[i]
    // (frames muestras mono), sintetiza el campo sobre el array y mezcla el
    // resultado binaural acumulando en outL/outR (no sobrescribe: suma,
    // para componer con el resto de la cadena espacial).
    void process(const float* const* objectInputs, int numObjects,
                 float* outL, float* outR, int frames) noexcept;

    int  numSpeakers() const noexcept { return numSpeakers_; }
    int  numObjects()  const noexcept { return static_cast<int>(objects_.size()); }

private:
    struct PrimarySource {
        int   id = -1;
        float x = 0.f, y = 0.f;
        float gain = 1.f;
    };

    float sampleRate_ = 48000.f;
    int   blockSize_  = 384;
    int   numSpeakers_ = 16;

    std::vector<PrimarySource> objects_;

    // Geometría del array: azimut por altavoz (rad, 0 = frente).
    std::array<float, kMaxSpeakers> speakerAzimuth_{};
    // Factor de panning binaural por altavoz (ILD senoidal) e ITD (s).
    std::array<float, kMaxSpeakers> speakerGainL_{}, speakerGainR_{};
    std::array<int,   kMaxSpeakers> speakerItdL_{}, speakerItdR_{}; // en muestras

    // Líneas de delay por (objeto × altavoz) — driving signal retardada.
    // Tamaño máximo por línea: delay máximo físico del array (~(2R+maxDist)/c).
    std::vector<std::vector<float>> delayLines_;   // [objIdx*numSpeakers + spk][tap]
    std::vector<int>                delaySamples_; // delay entero por par
    std::vector<float>              delayFrac_;    // fracción para interp lineal
    std::vector<float>              drivingGain_;  // ganancia de driving por par
    std::vector<int>                writePos_;     // cursor de escritura por línea
    std::vector<float>              speakerBus_;   // acumulador por altavoz
    int maxDelayTap_ = 0;

    // Historia ITD por altavoz (64 muestras — ITD máximo ~30 @48k).
    std::array<std::array<float, 64>, kMaxSpeakers> itdHist_{};
    std::array<int, kMaxSpeakers>                   itdPos_{};

    void rebuildGeometry() noexcept;   // altavoces: azimut, ILD, ITD
    void rebuildDelays() noexcept;     // (re)asigna líneas tras setObject
};

} // namespace ivanna::spatial
