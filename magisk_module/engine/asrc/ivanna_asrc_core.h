// ============================================================================
// IVANNA OMEGA SUPREME — ASRC core (la MANO del sistema)
// ivanna_asrc_core.h — v2.5.396
//
// DISEÑO DE BLOQUE FIJO (esto es lo que hace que funcione de verdad):
//   Un efecto de AudioFlinger DEBE devolver exactamente los mismos frames que
//   recibe. Un resampler "ingenuo" que devuelve N*ratio frames provoca un
//   desgarro en cada frontera de bloque (medido: THD+N -30 dB, el fallo que
//   esta versión corrige). Por eso el núcleo es un FIFO + lectura fraccionaria
//   con bucle de fase:
//
//     * sale SIEMPRE in_frames (bloque fijo, sin desgarro)}
//     * se consume in_frames * step del FIFO, con step = 1/ratio
//     * el nivel del FIFO se corrige con un lazo proporcional (efecto
//       equivalente a insertar/quitar una muestra cada ~3 s a 70 ppm, pero sin
//       clic: la corrección se reparte en todo el bloque en vez de saltar)
//
//   De ahí: frecuencia corregida Y bloque intacto Y sin artefacto audible.
//   Las tres cosas, medidas en los tests (1, 2, 3 y 4).
//
// FAIL-CLOSED (§5.5):
//   ratio == 1.0 -> memcpy bit-exacto, ni un LSB de diferencia.
//   ratio inválido (NaN/inf/fuera de ±500 ppm), kill switch, IPC caído,
//   bloque viejo -> el llamador pone 1.0 y el audio pasa intacto.
// ============================================================================
#ifndef IVANNA_ASRC_CORE_H
#define IVANNA_ASRC_CORE_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ivanna {
namespace asrc {

constexpr double kMaxPpm = 500.0;

struct Config {
    int taps = 319;          // FIR polifásico (Ruta A v2.5.427); 287 = HAL v6
    int phases = 128;        // sub-fases por muestra de entrada
    int psi_taps = 64;       // Ψ por oído (§3.1)
    double cutoff = 0.45;    // corte normalizado a Nyquist
    double kaiser_beta = 8.0;
    bool psi_enabled = false;
    int target_pending = 0;  // 0 -> se calcula como taps
};

struct ProcessStats {
    size_t in_frames = 0;
    size_t out_frames = 0;
    bool passthrough = false;
    double step = 1.0;        // muestras de entrada consumidas por muestra de salida
    double fifo_frames = 0;   // holgura del FIFO al terminar (telemetría)
    size_t underflow_frames = 0;  // frames rellenados por falta de datos: debe ser 0
};

class Core {
public:
    explicit Core(const Config& c = Config());
    ~Core();
    Core(const Core&) = delete;
    Core& operator=(const Core&) = delete;

    bool init();

    double setRatio(double ratio);
    double ratio() const { return ratio_; }
    void setBypass(bool b) { bypass_ = b; }
    bool bypassed() const { return bypass_; }
    void setPsiEnabled(bool b) { psi_enabled_ = b; }
    bool psiEnabled() const { return psi_enabled_; }

    // Salida: SIEMPRE in_frames (o menos sólo si out_capacity < in_frames).
    ProcessStats processS16(const int16_t* in, size_t in_frames,
                            int16_t* out, size_t out_capacity, int channels = 2);
    ProcessStats processF32(const float* in, size_t in_frames,
                            float* out, size_t out_capacity, int channels = 2);

    double groupDelayMs(double fs) const {
        return (cfg_.taps - 1) / 2.0 / fs * 1000.0;
    }
    void reset();

private:
    Config cfg_;
    bool inited_ = false;
    bool bypass_ = false;
    bool psi_enabled_ = false;
    double ratio_ = 1.0;

    std::vector<float> coef_;   // phases x taps
    std::vector<float> psi_;    // 2 x psi_taps
    std::vector<float> buf_;    // entrelazado, pendiente de consumir
    size_t buf_frames_ = 0;     // frames válidos en buf_
    double read_pos_ = 0.0;     // posición de lectura en frames dentro de buf_
    int channels_ = 2;

    void ensureInit(int channels);
    void push(const float* inter, size_t frames, int ch);
    void compact();
    ProcessStats emitFixed(const float* in, size_t in_frames,
                           float* out, size_t out_capacity, int ch);
    void applyPsi(float* buf, size_t frames, int channels);
};

bool ratioIsValid(double ratio);

}  // namespace asrc
}  // namespace ivanna

#endif
