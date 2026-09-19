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
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ivanna::wfs {
// ═══ Guarda de anti-aliasing espacial ═══════════════════════════════════
// WFS con array discreto de N altavoces sufre aliasing espacial por encima de
// f_alias = c / (2 * dx), dx = separación entre altavoces. Por encima, el
// frente de onda se reconstruye con artefactos (fantasmas). Solución de
// referencia (Spors & Ahrens): atenuación suave por encima de f_alias con un
// one-pole por canal, calculada desde la geometría real del array. No
// degrada el audio por debajo de f_alias — solo evita que la reconstrucción
// mienta arriba.
inline float spatialAliasCutoffHz(float speakerSpacingM, float c = 343.0f) noexcept {
    return (speakerSpacingM > 0.f) ? c / (2.f * speakerSpacingM) : 8000.f;
}
// One-pole lowpass por canal: y += a(x - y), a = 1 - exp(-2π f_c / sr)
struct SpatialAliasGuard {
    float a = 1.f, yL = 0.f, yR = 0.f;
    void init(float spacingM, float sampleRate) noexcept {
        const float fc = spatialAliasCutoffHz(spacingM);
        a = 1.f - std::exp(-2.f * 3.14159265f * fc / sampleRate);
    }
    inline void process(float& L, float& R) noexcept {
        yL += a * (L - yL); yR += a * (R - yR); L = yL; R = yR;
    }
    void reset() noexcept { yL = yR = 0.f; }
};
} // namespace ivanna::wfs

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

    // Fase 2 (geometría 3D real): sustituye el array circular generado por
    // 7 posiciones explícitas relativas al oyente (metros; dyFwd=frente+,
    // dz=altura respecto al oído). Distancia/retardo/ganancia usan la
    // distancia 3D real (incluye altura) — no solo el plano horizontal.
    void setSpeakerLayout3D(const float* dx, const float* dyFwd, const float* dz,
                             int count) noexcept;

    // Activación/desactivación del renderer completo (independiente de las
    // fuentes individuales) — ver blendWithBypass(). Arranca en 1/1
    // (activo y asentado): quien use process() sin tocar setEnabled()
    // obtiene el comportamiento de siempre (compatibilidad con el uso
    // existente de esta clase).
    void setEnabled(bool enabled) noexcept { enabledTarget_ = enabled ? 1.0f : 0.0f; }
    bool isEnabled() const noexcept { return enabledTarget_ > 0.5f; }
    bool isSettledBypassed() const noexcept {
        return enabledTarget_ <= 0.0f && enabledMix_ <= 0.0f;
    }
    // Mezcla bypass<->WFS con transición suave (~15 ms, smoothstep: derivada
    // cero en ambos extremos). wetL/R = salida de process() para el MISMO
    // bloque (buffer inicializado a 0 antes de process()). in-place seguro.
    void blendWithBypass(const float* dryL, const float* dryR,
                          const float* wetL, const float* wetR,
                          float* outL, float* outR, int frames) noexcept;

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
        float envFrom = 1.f;            // envolvente al inicio del bloque
        float envDelta = 0.f;           // target - from (transicion de 1 bloque)
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
    // Fase 2 (geometría 3D real): posición relativa al oyente por altavoz,
    // en uso cuando explicitLayout_==true (setSpeakerLayout3D). dz = altura
    // respecto al oído — entra en la distancia 3D real (no solo horizontal).
    bool  explicitLayout_ = false;
    std::array<float, kMaxSpeakers> speakerRelX_{}, speakerRelY_{}, speakerRelZ_{};

    // Activación/desactivación del renderer completo — ver setEnabled()/
    // blendWithBypass(). Arranca en 1/1: process() sin tocar setEnabled()
    // se comporta exactamente como antes de esta feature (compatibilidad).
    float enabledTarget_ = 1.0f;
    float enabledMix_    = 1.0f;

    // Taps (fuente × altavoz) — matriz plana [slot*kMaxSpeakers + spk],
    // preasignada en init() (kMaxObjects × kMaxSpeakers entradas fijas).
    std::vector<DelayTap> taps_;
    int maxDelayTap_ = 0;

    // Historia ITD por altavoz (64 muestras — ITD máximo ~30 @48k).
    std::array<std::array<float, 64>, kMaxSpeakers> itdHist_{};
    std::array<int, kMaxSpeakers>                   itdPos_{};

    // FIX (2026-09-18 → cableado real): antes definido en el .cpp, fuera de
    // esta clase y de su namespace — matemática correcta, jamás llamado
    // desde process(). Verificado: con la geometría por defecto (16
    // altavoces, radio 1.5 m) la separación es ~0.59 m → f_alias ≈ 291 Hz,
    // muy por debajo del rango donde vive la mayoría de la energía de
    // música/voz real — sin este filtro, prácticamente todo el contenido
    // audible se reconstruye por encima de la frecuencia de aliasing
    // espacial del array (imágenes fantasma / coloración tipo comb).
    ivanna::wfs::SpatialAliasGuard aliasGuard_;

    // Limitador suave de seguridad (anti-clip => anti-tronidos): por debajo
    // de |1.0| es EXACTAMENTE lineal (cero coloracion); por encima comprime
    // de forma continua hacia un techo asintotico de 2.0. La suma coherente
    // de N altavoces frontales (focus=cos, ILD x1.41, gain<=2) podia superar
    // +-2.8 -> clip duro en el destino = tronido. Esto lo vuelve imposible.
    static inline float softLimit(float x) noexcept {
        if (x >  1.0f) return  1.0f + (x - 1.0f) / (1.0f + (x - 1.0f));
        if (x < -1.0f) return -1.0f + (x + 1.0f) / (1.0f - (x + 1.0f));
        return x;
    }

    void rebuildGeometry() noexcept;            // altavoces: azimut, ILD, ITD
    void computeTapTargets(int slot) noexcept;  // delays/ganancias objetivo
    int  findSlot(int id) const noexcept;       // -1 si no existe
    int  allocSlot() noexcept;                  // -1 si está lleno
};

} // namespace ivanna::spatial
