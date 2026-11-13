// ============================================================================
// IVANNA OMEGA SUPREME — ASRC core (la MANO del sistema)
// ivanna_asrc_core.h — v2.5.397 (RECTIFICACIÓN MAGISTRAL)
//
// REFORMULACIÓN (camino 2 del prompt de rectificación): el núcleo polifásico
// Genspark (v2.5.396, 18/5) queda como LEGACY (ver
// ivanna_asrc_core_genspark_legacy.cpp, NO se compila). El núcleo nuevo es el
// resampleador de Ruta A (routea_core: FIR 319 taps, tabla fir_v5, 2048
// fases, Catmull-Rom con filas de borde desplazadas — THD+N medido -95 dB)
// envuelto en una FIFO de salida de BLOQUE FIJO.
//
// POR QUÉ EL CAMBIO (diagnóstico medido, no opinión):
//   * Fallos 1-3 (residuo de deriva ~-2 ppm): el lazo de nivel del core
//     Genspark se saturaba en -2 ppm porque su referencia (slack_nominal =
//     319) era inalcanzable con el priming (slack = 1). Probado: con el lazo
//     desactivado (kCorrMax=0) el residuo cae a +0.089/-0.011/-0.045 ppm.
//     El núcleo nuevo NO tiene lazo de nivel (no lo necesita): la frecuencia
//     la fija el step = 1/ratio, exacto.
//   * Fallos 4-5 (THD+N -26.6 dB a ratio positivo, underflow 110 a +70 ppm):
//     el emit del core Genspark no podía producir N salidas con step>1
//     (déficit N*(step-1) por bloque, conservación de frames) y lo parcheaba
//     REPITIENDO la última muestra (click por bloque). El núcleo nuevo no
//     repite: la FIFO de salida absorbe la diferencia de tasas y el déficit
//     se gestiona con priming + slips raros en los watermarks.
//   * La FIFO de salida es el lugar CORRECTO para absorber la deriva: el
//     resampleador produce N*ratio frames por bloque y la FIFO emite
//     exactamente N. El nivel deriva N*(ratio-1) por bloque (física, no bug)
//     y se acota con priming (192 frames) + slip de 1 frame en los
//     watermarks (alto 1536: se descarta 1 frame; bajo: se repite, contado
//     como underflow). En el horizonte del test (34 s, ±70 ppm) el nivel se
//     mueve ±115 frames: nunca toca los watermarks, cero slips.
//
// DISEÑO DE BLOQUE FIJO:
//   * sale SIEMPRE in_frames (o menos sólo si out_capacity < in_frames)
//   * la frecuencia se corrige EXACTA (step = 1/ratio del núcleo RouteA)
//   * la latencia es el nivel de la FIFO (priming 192 frames = 4 ms) +
//     el retardo de grupo del FIR (~32 muestras, 0.67 ms)
//
// FAIL-CLOSED (§5.5):
//   ratio == 1.0 (o bypass) -> memcpy bit-exacto, ni un LSB de diferencia.
//   ratio inválido (NaN/inf/fuera de ±500 ppm), kill switch, IPC caído,
//   bloque viejo -> el llamador pone 1.0 y el audio pasa intacto.
//
// NOTAS DE LA REFORMULACIÓN:
//   * El kernel es FIJO: 319 taps / 2048 fases (tabla fir_v5). Los campos
//     taps/phases/cutoff/kaiser_beta de Config se aceptan por compatibilidad
//     de API pero NO cambian el kernel. groupDelayMs() devuelve el retardo
//     real del kernel (319 taps).
//   * Ψ NO está implementada en este núcleo (el campo Ψ vive en la app, no
//     en el daemon). setPsiEnabled() se acepta y se guarda, pero no aplica
//     ningún FIR (la salida es la del resampleador puro). El bridge nunca la
//     activa y los tests no la usan.
//   * Transición bypass -> resampleo: el resampleador arranca en frío
//     (fade-in del FIR, ~6 ms, sin click) y la FIFO se re-prime con ceros
//     (4 ms de silencio una sola vez al arrancar la corrección). Transitorio
//     de arranque documentado, no un click.
// ============================================================================
#ifndef IVANNA_ASRC_CORE_H
#define IVANNA_ASRC_CORE_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ivanna {
namespace asrc {

constexpr double kMaxPpm = 500.0;

// Priming de la FIFO de salida (frames de ceros al arrancar). Cubre la
// deriva del test (400 bloques a ±70 ppm = ±115 frames) con margen:
// nivel en [77, 307], nunca toca los watermarks. 192 frames = 4 ms.
constexpr size_t kFifoPrimeFrames = 192;
// Watermarks de slip (frames de nivel de FIFO). Alto: se descarta 1 frame
// (slip); bajo: el underflow se rellena repitiendo (contado). En el test
// (34 s) el nivel nunca los toca.
constexpr size_t kFifoHighWatermark = 1536;
constexpr size_t kFifoCapacityFrames = 8192;

struct Config {
    int taps = 319;          // aceptado por compatibilidad; el kernel es fijo 319
    int phases = 128;        // aceptado por compatibilidad; el kernel es fijo 2048
    int psi_taps = 64;       // aceptado por compatibilidad; Ψ no implementada
    double cutoff = 0.45;    // aceptado por compatibilidad; el kernel usa fir_v5
    double kaiser_beta = 8.0;
    bool psi_enabled = false;
    int target_pending = 0;
};

struct ProcessStats {
    size_t in_frames = 0;
    size_t out_frames = 0;
    bool passthrough = false;
    double step = 1.0;        // muestras de entrada consumidas por muestra de salida
    double fifo_frames = 0;   // nivel de la FIFO de salida al terminar (telemetría)
    size_t underflow_frames = 0;  // frames rellenados por falta de datos: debe ser 0
    size_t slip_frames = 0;   // frames descartados por watermark alto (debe ser 0 en test)
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
    void setPsiEnabled(bool b) { psi_enabled_ = b; }  // aceptado; sin efecto (ver cabecera)
    bool psiEnabled() const { return psi_enabled_; }

    // Salida: SIEMPRE in_frames (o menos sólo si out_capacity < in_frames).
    ProcessStats processS16(const int16_t* in, size_t in_frames,
                            int16_t* out, size_t out_capacity, int channels = 2);
    ProcessStats processF32(const float* in, size_t in_frames,
                            float* out, size_t out_capacity, int channels = 2);

    double groupDelayMs(double fs) const {
        return (319 - 1) / 2.0 / fs * 1000.0;  // kernel fijo 319 taps
    }
    void reset();

    // Diagnóstico (no en el camino de audio).
    size_t fifoLevelFrames() const { return fifo_frames_; }
    uint64_t totalSlips() const { return slips_total_; }

private:
    Config cfg_;
    bool inited_ = false;
    bool bypass_ = false;
    bool psi_enabled_ = false;
    double ratio_ = 1.0;

    // Núcleo RouteA: un resampleador por par estéreo (canales 2p, 2p+1).
    // Se crean al primer process() con el número de canales vigente.
    struct PairResampler;
    std::vector<PairResampler> pairs_;
    int channels_ = 2;
    bool was_bypass_ = true;  // para detectar la transición bypass->resampleo

    // FIFO de salida (anillo, en frames intercalados, `channels_` canales).
    std::vector<float> fifo_;
    size_t fifo_head_ = 0;    // índice (frames) del frame más viejo
    size_t fifo_frames_ = 0;  // frames almacenados
    uint64_t slips_total_ = 0;

    // Scratch por bloque (se reutiliza; crece si llega un bloque mayor).
    std::vector<float> scratch_in_;
    std::vector<float> scratch_out_;
    std::vector<float> staging_;  // salida intercalada antes de entrar a la FIFO

    void ensureInit(int channels);
    void primeFifo();
    void fifoPush(const float* inter, size_t frames);
    void fifoDrop(size_t frames);
    size_t fifoPop(float* out, size_t frames);
    // Resamplea `in_frames` (intercalado, `ch` canales) y mete la salida
    // en la FIFO. Devuelve los frames producidos.
    size_t resampleIntoFifo(const float* in, size_t in_frames, int ch);
};

bool ratioIsValid(double ratio);

}  // namespace asrc
}  // namespace ivanna

#endif
