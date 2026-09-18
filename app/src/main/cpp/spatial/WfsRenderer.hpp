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
// ── REFINAMIENTO MAGISTRAL: eliminación de micro-cortes (clicks/pops) ──
// CAUSA RAÍZ del defecto: la versión anterior llamaba a rebuildDelays()
// desde setObject(), y ese método hacía assign() de TODAS las líneas de
// delay → cada cambio de posición de una fuente BORRABA el historial de
// audio (líneas a cero, cursores a cero) → discontinuidad instantánea de
// amplitud = micro-corte audible. Además reasignaba memoria en caliente.
//
// Arquitectura glitch-free:
//  1. UNA línea de delay persistente POR FUENTE (no por par fuente×altavoz:
//     todos los taps de una fuente leen la misma señal — mismo resultado
//     físico con 1/16 de memoria y de escrituras). Jamás se resetea cuando
//     la fuente se MUEVE: el audio en propagación sigue su curso natural.
//  2. Retardo fraccionario SUAVIZADO: el delay objetivo se recalcula al
//     mover la fuente, pero el delay EFECTIVO lo persigue con un one-pole
//     por muestra (~5 ms) → glide sin salto de fase (Doppler físico leve,
//     perceptualmente correcto, cero click).
//  3. Ganancia de driving SUAVIZADA con la misma envolvente → sin salto de
//     amplitud al cruzar semiespacios del array.
//  4. Fade-out de un bloque al ELIMINAR una fuente (la salida natural por
//     historial-a-ceros cubre el nacimiento; el corte de una fuente sonando
//     era el otro click clásico).
//  5. CERO asignaciones de memoria en process(): toda la memoria se
//     preasigna en init() o al registrar una fuente (hilo de control).
//
// Coste: O(objetos × altavoces × frames), ~25k MACs por bloque de 384
// frames con 4 objetos × 16 altavoces — viable en el audio thread.
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
    static constexpr int   kMaxObjects    = 32;   // ranuras fijas de fuentes
    static constexpr float kSpeedOfSound  = 343.0f;    // m/s
    static constexpr float kArrayRadiusM  = 1.5f;      // radio del array virtual
    static constexpr float kRefDistanceM  = 1.0f;      // distancia de referencia 0 dB
    static constexpr float kMaxObjectDist = 8.0f;      // clamp de distancia de fuente
    static constexpr float kMaxItdS       = 0.00063f;  // ITD máximo interaural
    // Constante del one-pole de suavizado de delay/ganancia (~5 ms @48k):
    // lo bastante rápida para seguir gestos de UI, lo bastante lenta para
    // que el cambio de delay sea un glide inaudible en vez de un salto.
    static constexpr float kSmoothCoeff   = 0.004f;

    bool init(float sampleRate, int blockSize, int numSpeakers = 16) noexcept;
    void reset() noexcept;

    // Posición/actualización de fuente primaria. x/y en metros relativos al
    // oyente (x: derecha+, y: frente+). gain lineal. thread-safety: llamar
    // desde el hilo de control entre bloques. NUNCA resetea historiales:
    // mover una fuente suaviza delay/ganancia sin tocar el audio en vuelo.
    void setObject(int id, float x, float y, float gain) noexcept;
    void removeObject(int id) noexcept;   // fade-out de un bloque, sin click
    void clearObjects() noexcept;

    // Procesa UN bloque acumulando en outL/outR (suma, no sobrescribe,
    // para componer con el resto de la cadena espacial). objectInputs[i]
    // corresponde a la i-ésima fuente viva en orden de registro — misma
    // semántica de orden que la versión anterior.
    void process(const float* const* objectInputs, int numObjects,
                 float* outL, float* outR, int frames) noexcept;

    int  numSpeakers() const noexcept { return numSpeakers_; }
    int  numObjects()  const noexcept { return numActiveObjects_; }

private:
    // Estado de UNA fuente primaria (ranura persistente, índice fijo).
    // La línea de delay es POR FUENTE: todos los altavoces leen la misma
    // señal retardada cada uno con su propio delay fraccionario.
    struct SourceSlot {
        int   id = -1;
        bool  active = false;
        float x = 0.f, y = 0.f;
        float gain = 1.f;
        // Envolvente de salida (fade-out glitch-free al eliminar). 0..1.
        float actEnv = 1.f;
        float actTarget = 1.f;
        float envStep = 0.f;            // paso lineal por muestra del bloque
        std::vector<float> line;        // historial circular de la fuente
        int   writePos = 0;
    };

    // Par (fuente × altavoz): solo estado suavizado (la señal vive en el
    // slot de la fuente — compartida por los 32 taps de esa fuente).
    struct DelayTap {
        float delayTarget = 0.f;   // delay físico objetivo (muestras, frac)
        float delaySmooth = 0.f;   // delay efectivo suavizado (muestras, frac)
        float gainTarget  = 0.f;   // driving gain objetivo
        float gainSmooth  = 0.f;   // driving gain suavizada
    };

    float sampleRate_ = 48000.f;
    int   blockSize_  = 384;
    int   numSpeakers_ = 16;

    std::array<SourceSlot, kMaxObjects> slots_{};
    int numActiveObjects_ = 0;

    // Geometría del array.
    std::array<float, kMaxSpeakers> speakerAzimuth_{};
    std::array<float, kMaxSpeakers> speakerGainL_{}, speakerGainR_{};
    std::array<int,   kMaxSpeakers> speakerItdL_{}, speakerItdR_{}; // en muestras

    // Taps (fuente × altavoz) — matriz plana [slot*kMaxSpeakers + spk],
    // preasignada en init() (kMaxObjects × kMaxSpeakers entradas fijas).
    std::vector<DelayTap> taps_;
    int maxDelayTap_ = 0;

    // Historia ITD por altavoz (64 muestras — ITD máximo ~30 @48k).
    std::array<std::array<float, 64>, kMaxSpeakers> itdHist_{};
    std::array<int, kMaxSpeakers>                   itdPos_{};

    void rebuildGeometry() noexcept;            // altavoces: azimut, ILD, ITD
    void computeTapTargets(int slot) noexcept;  // delays/ganancias objetivo
    int  findSlot(int id) const noexcept;       // -1 si no existe
    int  allocSlot() noexcept;                  // -1 si está lleno
};

} // namespace ivanna::spatial
