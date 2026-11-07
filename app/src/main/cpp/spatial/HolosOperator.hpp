// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#pragma once
// HolosOperator — HOLOS · Operador Único de Campo Acústico.
//
// FUSION.md §4–§5: doce funciones, una sola matemática. Por banda ERB b
// y frame t, con X_b(t) el espectro de la banda (mid/side):
//
//   Y_b(t) = G_b(s_b) ⊛ [ K_b(s_b) · X_b(t) ] · Γ_b(s_b)
//
// - G_b: función de Green unificada (HolosGreenKernel): ITD/ILD esféricos,
//   pinna medida, (1/r)^γ, difuso → cola compartida (RirConvolver).
// - K_b: kernel de Volterra por banda (HolosVolterraKernel): polinomio
//   Cheb/GoldenEar/Padé + memoria Heun + Volterra H2 (K=64, sin kernel
//   → identidad). Se evalúa en el dominio temporal sobre la señal ya
//   renderizada (post-OLA): un polinomio por bin no genera armónicos.
// - Γ_b: campo de ganancia único (HolosGainField): estática × dinámica
//   × ancho × techo, desde el mismo vector de estado s_b.
// - s_b: un solo vector de estado por banda (HolosStateEstimator).
//
// Flujo: STFT hop 128 / FFT 512 escalable con SR (misma rejilla que ORBE,
// misma FFTRadix2) → estimación de s_b → render por bin → IFFT +
// overlap-add (Hann, COLA (N/hop)·0.375) → K temporal + softCeil sobre el
// hop renderizado → mezcla wet/dry de potencia constante con el camino
// legacy (crossfade 15 ms, mismo patrón que el upmixer y que ORBE).
// amount = 0 es bit-exacto con la entrada. Latencia total = fftSize
// muestras (512 @48 kHz = 10.7 ms).
//
// Entra en FusionCore EN EL LUGAR DE ORBE (tras el blend WFS, antes del
// M/S), por el mismo motivo que ORBE está ahí. Cuando HOLOS está activo,
// ORBE no corre (sería doble render con doble retardo de 512); cuando
// HOLOS está en 0, la cadena legacy queda intacta (sin borrar código
// legacy todavía — FUSION §10 paso 5: cableado conmutado, no borrado en
// caliente; la eliminación de módulos es fase 3, post-GREEN).
//
// Tiempo real: cero malloc, cero locks, cero I/O en process(); todo se
// preasigna en prepare(). Estado por instancia, nunca global.
// Falla cerrada (fail-closed): coherencia media < 0.3, parámetros no
// finitos o thermal shed → wet = 0 (camino legacy intacto). Cada término
// cae a identidad por separado; nunca a silencio ni a ruido.
//
// Término difuso (v1): UNA sola convolución particionada compartida
// (RirConvolver, IR sintetizada Master Studio, rt60 = holos_room_rt60),
// alimentada con el mid. La salida del RIR es dry·0.32 + wet·0.82 con
// wetNow=1 (su mezcla interna), así que el wet puro se recupera como
// (out − 0.32·mid)/0.82; el wetNow se lleva a 1.0 en prepare() alimentando
// silencio (si no, los primeros ~10 ms tras activar mezclarían seco en
// la cola). La cola se analiza con una segunda STFT (solo FFT) y se suma
// por bin con (1−coh_b)·diffuse·0.82. El RIR se alimenta siempre que
// HOLOS está activo (no solo con diffuse>0) para que su historia
// overlap/FDL no se quede obsoleta.
//
// Tiers térmicos (FUSION §7.3, en orden): NORMAL todo on (24 bandas, H3
// on, difuso on); WARM 65–70 °C: bandas 24→12, H3 off, diffuse ×0.5;
// LIMITED 70–75 °C: difuso off, gamma fijo 1.0 (Volterra K=64→32 queda
// pendiente de que exista un kernel: sin kernel el término ya es
// identidad); BYPASS >75 °C: HOLOS off → cadena legacy bit-exacta.
// Las tablas de bandas 24 y 12 se construyen AMBAS en prepare() y el
// cambio es inmediato (a diferencia de ORBE, donde bandCount solo se
// aplica en el próximo prepare).
//
// Honestidad de la v1 (mismas reglas que ORBE): sin término de magnitud
// de pinna hasta que se cargue una tabla medida (PinnaTable vacía → 0 dB);
// sin interpolación OT de pinna (lineal, fase 2); sin kernel Volterra por
// banda (K = polinomio memoryless + Volterra banda ancha actual, que hoy
// es identidad); DRR_b es un proxy desde coherencia (su uso como
// modulador queda para fase 2); la equivalencia de fase entre el FIR 256
// taps del EvoEQ y la magnitud por bin de Γ no está medida (§5.5).
// Todo off por defecto (amount=0) hasta que la puerta auditiva dé GREEN.
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "HolosGainField.hpp"
#include "HolosGreenKernel.hpp"
#include "HolosStateEstimator.hpp"
#include "HolosVolterraKernel.hpp"
#include "NearFieldHeadModel.hpp"
#include "RirConvolver.hpp"
#include "fft_radix2.hpp"

namespace ivanna {

struct HolosParams {
    float amount = 0.0f;       // 0..1 wet/dry vs legacy; 0 = bit-exacto
    float distanceM = 0.35f;   // 0.15..2.0 m, distancia base ("íntimo")
    float spread = 1.0f;       // 0..1.5, escala del azimut estimado
    float proximityDb = 3.0f;  // 0..6 dB, techo del realce de proximidad
    float diffuse = 0.5f;      // 0..1, cuánto campo difuso va a la cola compartida
    float harmonics = 0.32f;   // 0..1, mezcla del término K
    float warmth = 0.5f;       // 0..1, balance par/impar de K
    float dynamics = 1.0f;     // 0..1, cantidad de Γ_dinámico (1 = compresor como hoy)
    float width = 1.0f;        // 0..1.5, escala de Γ_ancho
    float gamma = 1.0f;        // 0.5..1.0, exponente de divergencia (1 esférico, ½ WFS)
    float roomRt60 = 0.34f;    // 0.18..1.8 s, RT60 del término difuso (próximo prepare)
    bool headTracking = true;  // parallax por setHeadPose()
    int bandCount = 24;        // 12 o 24 bandas ERB
    // EQ estático (Γ_estático, plano por defecto = no-op bit-exacto).
    float eqLow = 0.0f, eqMid = 0.0f, eqHigh = 0.0f, eqPresence = 0.0f;
    float eqTiltDb = 0.0f, eqFreq = 1000.0f;
};

enum class HolosThermalTier : int {
    Normal = 0,   // < 65 °C: todo on
    Warm = 1,     // 65–70 °C: 12 bandas, H3 off, diffuse ×0.5
    Limited = 2,  // 70–75 °C: difuso off, gamma fijo 1.0
    Bypass = 3,   // > 75 °C: HOLOS off → legacy bit-exacto
};

class HolosOperator {
public:
    static constexpr int kHop = 128;  // == Ivanna::BLOCK_SIZE
    static constexpr int kMaxBands = 24;

    void prepare(float sr);
    void reset() noexcept;
    void setParams(const HolosParams& p) noexcept;
    void setAmount(float amount) noexcept;
    void setHeadPose(float yawRad) noexcept;
    void setThermalTier(HolosThermalTier tier) noexcept;
    void setThermalShed(bool shed) noexcept;  // compat: true = Bypass
    void setPinnaTable(const holos::PinnaTable& table);
    // Carga de kernel Volterra IVK1 (hilo de CONTROL, nunca el de audio).
    int loadVolterraKernel(const uint8_t* package, size_t packageSize) noexcept;
    // process() acepta cualquier n > 0; siempre escribe exactamente n
    // muestras. Con amount = 0 y el fade completado es passthrough
    // bit-exacto (sin tocar los buffers).
    void process(float* l, float* r, int n) noexcept;

    // Diagnóstico / tests.
    int fftSize() const noexcept { return fftSize_; }
    int latencySamples() const noexcept { return fftSize_; }
    float sampleRate() const noexcept { return sampleRate_; }
    int activeBandCount() const noexcept { return activeBands_; }
    HolosThermalTier thermalTier() const noexcept {
        return (HolosThermalTier)tier_.load(std::memory_order_relaxed);
    }
    const holos::HolosBandState& bandState(int b) const noexcept {
        return estimator_.state(b);
    }
    float bandGammaDb(int b) const noexcept;  // Γ aplicado (dB, estático+dinámico)
    float bandWidthGain(int b) const noexcept;
    float bandDiffuseMix(int b) const noexcept;
    float meanCoherence() const noexcept { return estimator_.meanCoherence(); }
    float appliedAmount() const noexcept { return wet_; }
    bool isActive() const noexcept { return wet_ > 0.0f || blend_ > 0.0f; }
    bool hasVolterraKernel() const noexcept { return kernel_.hasVolterraKernel(); }

private:
    struct BandSet {
        int loBin[kMaxBands] = {};
        int hiBin[kMaxBands] = {};
        float centerHz[kMaxBands] = {};
        std::vector<int> bandOfBin;  // por bin 0..fftSize-1
        int count = 24;
    };

    void rebuildBandTables();
    void applyActiveBandSet();
    void runFrame();
    void computeBandGains();
    void renderBins(float* reL, float* imL, float* reR, float* imR,
                    const float* reTL, const float* imTL,
                    const float* reTR, const float* imTR);

    float sampleRate_ = 48000.0f;
    int fftSize_ = 512;
    bool prepared_ = false;

    std::atomic<float> amount_{0.0f};
    std::atomic<float> distanceM_{0.35f};
    std::atomic<float> spread_{1.0f};
    std::atomic<float> proximityDb_{3.0f};
    std::atomic<float> diffuse_{0.5f};
    std::atomic<float> harmonics_{0.32f};
    std::atomic<float> warmth_{0.5f};
    std::atomic<float> dynamics_{1.0f};
    std::atomic<float> width_{1.0f};
    std::atomic<float> gamma_{1.0f};
    std::atomic<float> roomRt60_{0.34f};
    std::atomic<bool> headTracking_{true};
    std::atomic<bool> h3Enabled_{true};
    std::atomic<float> yawRad_{0.0f};
    std::atomic<int> bandCount_{24};
    std::atomic<int> tier_{(int)HolosThermalTier::Normal};
    std::atomic<float> eqLow_{0.0f}, eqMid_{0.0f}, eqHigh_{0.0f}, eqPresence_{0.0f};
    std::atomic<float> eqTiltDb_{0.0f}, eqFreq_{1000.0f};

    // Núcleos (estado por instancia).
    holos::HolosStateEstimator estimator_;
    holos::HolosVolterraKernel kernel_;
    holos::PinnaTable pinna_;
    Ivanna::RirConvolver rir_;  // término difuso: una sola IR compartida

    // Estado STFT principal (todo preasignado en prepare()).
    std::unique_ptr<FFTRadix2> fft_;
    std::vector<float> window_;
    std::vector<float> ringL_, ringR_;
    int ringPos_ = 0;
    std::vector<float> pendL_, pendR_;
    int pendCount_ = 0;
    std::vector<float> olaL_, olaR_;
    std::vector<float> wetQL_, wetQR_;
    std::vector<float> dryQL_, dryQR_;
    int qHead_ = 0, qTail_ = 0, qCount_ = 0, qCap_ = 0;
    int frameIndex_ = 0;
    int skipEmit_ = 0;
    float colaInv_ = 1.0f / 1.5f;
    std::vector<float> reL_, imL_, reR_, imR_;

    // Segunda STFT (solo análisis) para la cola difusa.
    std::vector<float> tailRingL_, tailRingR_;
    int tailRingPos_ = 0;
    std::vector<float> tailReL_, tailImL_, tailReR_, tailImR_;
    std::vector<float> midL_, midR_;  // scratch: mid → RIR → wet recuperado

    // Tablas de bandas (24 y 12; el tier cambia el set activo en caliente).
    BandSet set24_, set12_;
    const BandSet* activeSet_ = &set24_;
    int activeBands_ = 24;

    // Ganancias Γ por banda, calculadas 1×/frame (para render y tests).
    float gammaLin_[kMaxBands] = {};
    float gammaDb_[kMaxBands] = {};
    float widthSide_[kMaxBands] = {};
    float widthNorm_[kMaxBands] = {};
    float diffuseMix_[kMaxBands] = {};

    // Fades (15 ms): wet_ = cantidad aplicada, blend_ = 0 seco en vivo,
    // 1 seco retardado (alineado con el STFT).
    float wet_ = 0.0f;
    float blend_ = 0.0f;
    bool flushPending_ = false;
    size_t totalIn_ = 0;
    size_t totalOut_ = 0;
};

}  // namespace ivanna
