#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// spatial/RirConvolver.hpp — Convolucionador RIR overlap-save, real-time safe
//
// Implementa convolución linear eficiente con la respuesta al impulso de sala
// del RirDataset usando el algoritmo overlap-save en el dominio de la frecuencia.
//
// Propiedades de tiempo real:
//   - Sin malloc() en process() — toda la memoria se reserva en load()
//   - Sin locks — diseñado para el hilo de audio (actualización atómica de IR)
//   - Sin librerías externas — FFT Radix-2 de fft_radix2.hpp
//   - Wet/dry configurable en tiempo real (setWetDry sin re-convolución)
//
// Limitaciones documentadas:
//   - IR máximo: MAX_IR_LEN muestras (512 @ 16kHz, interpoladas a 48kHz)
//   - Bloque: BLOCK_SIZE (512 frames)
//   - Sólo estéreo (L + R independientes — misma IR para ambos canales)
// ─────────────────────────────────────────────────────────────────────────────

#include <cstddef>
#include <cstdint>
#include <vector>
#include <atomic>
#include <cstring>

namespace Ivanna {

class RirConvolver {
public:
    static constexpr int BLOCK    = 512;
    static constexpr int MAX_IR   = 512;   // particion 0 (head, latencia cero)
    static constexpr int FFT_SIZE = 1024;  // BLOCK + MAX_IR - 1, redondeado a pot2

    // ── Convolucion particionada no uniforme (estado del arte) ─────────────
    // Un RIR de sala real dura 0.3-2 s (>= 96k muestras @48k). Con un solo
    // segmento la latencia seria inaceptable. Solucion de referencia
    // (Gardner / Wefers): particionar el IR en head corto (latencia 0,
    // procesado en el bloque actual) + cola larga (overlap-save por bloques,
    // latencia de 1 bloque, inaudible en el rango de reverb).
    static constexpr int MAX_IR_TAIL  = 16384;            // ~341 ms @48k de cola
    static constexpr int TAIL_PARTS   = MAX_IR_TAIL / BLOCK; // 32 particiones de cola
    static constexpr int MAX_IR_TOTAL = MAX_IR + MAX_IR_TAIL;

    RirConvolver();

    // Carga una IR estéreo desde vectores ya resampleados a la SR del engine.
    // Se puede llamar desde el hilo de control (no durante process()).
    // Thread-safe con process() vía flag atómico pending_.
    void load(const float* irL, const float* irR, int irLen) noexcept;
    // Carga con IR larga: hasta MAX_IR_TOTAL muestras. Las primeras MAX_IR van
    // al camino head (latencia cero); el resto a las particiones de cola.

    // Descarga la IR — vuelve a bypass puro.
    void unload() noexcept;

    // Wet/dry [0,1]. 0=bypass, 1=sólo sala. Actualización atómica en tiempo real.

    // ── BRIR: decorrelación de la cola tardía (estado del arte) ────────────
    // Una reverb estéreo creíble necesita que las colas L y R NO sean copias
    // correladas (eso suena a "reverb mono dentro de la cabeza"). Se aplica una
    // red allpass decorreladora en frecuencia a la partición de cola de R:
    // rota la fase por bin (allpass), preservando magnitud — mismo RT60, pero
    // difusa y envolvente. 0.0 = correlada (legado), 1.0 = máxima difusión.
    void setLateDecorrelation(float amount) noexcept { decorrel_.store(std::clamp(amount, 0.f, 1.f), std::memory_order_relaxed); }
    float lateDecorrelation() const noexcept { return decorrel_.load(std::memory_order_relaxed); }
    void setWetDry(float wet) noexcept { wetDry_.store(wet, std::memory_order_relaxed); }
    float wetDry() const noexcept      { return wetDry_.load(std::memory_order_relaxed); }

    bool isLoaded() const noexcept { return loaded_.load(std::memory_order_acquire); }

    // Procesa un bloque de BLOCK frames estéreo.
    // L y R son buffers in-place — se modifican con la señal convolucionada.
    // Si !isLoaded() o wetDry()==0, es no-op (bypass perfecto).
    void process(float* L, float* R, int frames) noexcept;

private:
    // Tablas de seno/coseno para la FFT Radix-2 in-place.
    void fftReal(float* re, float* im, int n, bool inverse) noexcept;

    // Buffers de convolución overlap-save (float, no complex separado)
    float irReL_[FFT_SIZE] = {};  // Re(FFT(irL)) de longitud FFT_SIZE/2+1
    float irImL_[FFT_SIZE] = {};  // Im(FFT(irL))
    float irReR_[FFT_SIZE] = {};
    float irImR_[FFT_SIZE] = {};

    float overlapL_[MAX_IR] = {};  // Cola overlap-save canal L
    float overlapR_[MAX_IR] = {};
    int   overlapLen_ = 0;

    // Cola particionada: espectros de cada particion + FDL (frequency delay line)
    std::vector<float> tailIrReL_, tailIrImL_, tailIrReR_, tailIrImR_; // [TAIL_PARTS][FFT_SIZE]
    std::vector<float> fdlReL_, fdlImL_, fdlReR_, fdlImR_;             // espectros de entrada
    int tailPartsActive_ = 0;   // cuantas particiones de cola tienen energia
    int fdlIndex_ = 0;          // puntero circular del FDL

    float workRe_[FFT_SIZE] = {};  // Buffers de trabajo — sin malloc en process()
    float workIm_[FFT_SIZE] = {};

    std::atomic<float> wetDry_{0.f};
    std::atomic<float> decorrel_{0.0f};   // 0=cola correlada (legado), 1=difusa
    // Anti-zipper del wet/dry: el slider se aplica por BLOQUE (escalón duro
    // de ganancia = tronido). Se suaviza por muestra con un one-pole.
    float wetNow_    = 0.0f;   // wet efectivo suavizado (muestra a muestra)
    float wetSmooth_ = 0.0f;   // coef. one-pole; 0 = se deriva de sampleRate
    std::atomic<bool>  loaded_{false};
    std::atomic<bool>  pending_{false};

    // IR pendiente de aplicar (escrita por load(), leída por process())
    float pendIrReL_[FFT_SIZE] = {};
    float pendIrImL_[FFT_SIZE] = {};
    float pendIrReR_[FFT_SIZE] = {};
    float pendIrImR_[FFT_SIZE] = {};
    int   pendOverlapLen_ = 0;

    // FIX (tronidos, 2026-08-27): crossfade del IR al cambiar de sala.
    // Antes el swap borraba el overlap y cambiaba el IR en seco — la cola
    // de reverb se cortaba abruptamente = tronido audible. Ahora la IR
    // anterior se conserva y se funde con la nueva durante XFADE_BLOCKS
    // bloques (~43 ms @ 512/48k) en el dominio de la frecuencia; la cola
    // vieja (overlap) sigue sirviéndose sin cortes durante el fade.
    static constexpr int XFADE_BLOCKS = 4;  // 4 × 512 muestras ≈ 43 ms @48k
    float oldIrReL_[FFT_SIZE] = {};
    float oldIrImL_[FFT_SIZE] = {};
    float oldIrReR_[FFT_SIZE] = {};
    float oldIrImR_[FFT_SIZE] = {};
    int   xfadeBlocks_ = 0;  // >0 mientras dura el crossfade
};

} // namespace Ivanna
