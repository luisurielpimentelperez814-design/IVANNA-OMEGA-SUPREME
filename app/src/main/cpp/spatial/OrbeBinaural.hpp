// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#pragma once
// OrbeBinaural — ORBE: Binaural Esférico de Campo Cercano (Near-Field
// Spherical Binaural). Eleva el upmixing HOA+HRTF (la función más fuerte
// de IVANNA, activa por defecto) atacando su punto débil: el render actual
// es de campo lejano y distancia fija. ORBE coloca cada banda ERB a un
// azimut y una distancia continuos (0.15–2.0 m) con el modelo de cabeza
// esférica en campo cercano (NearFieldHeadModel), estima la escena de la
// propia grabación por banda (ILD, IPD→ITD, coherencia — familia
// DirAC/BCC, sin metadata) y aplica parallax por head-tracking CUANDO
// alguien alimenta la pose con setHeadPose(). En Ruta A nadie llama a
// setHeadPose() sobre la instancia de Ruta A (el yaw queda en 0 y el
// flag headTracking no tiene efecto audible); en Ruta B la pose llega
// por snapshot (daemon SET_HEAD_POSE) vía updateHeadPose().
//
// Flujo: STFT hop 128 / FFT 512 escalable con SR → estimación por banda
// (suavizado 20 ms) → render por bin (retardo = rotación de fase exacta,
// ganancia ILD + proximidad ≤ +6 dB) → IFFT + overlap-add (Hann, COLA
// (N/hop)·0.375 — 1.5 solo con N=512; el hop es fijo y N escala con SR)
// → mezcla wet/dry de potencia constante con el camino legacy.
// amount = 0 es bit-exacto con la entrada. Latencia total = fftSize
// muestras (512 @48 kHz = 10.7 ms): el hilo de salida la absorbe con un
// FIFO, así que process() siempre devuelve exactamente n muestras.
//
// Tiempo real: cero malloc, cero locks, cero I/O en process(); todo se
// preasigna en prepare(). Estado por instancia, nunca global.
// Falla cerrada (fail-closed): coherencia media < 0.3, parámetros no
// finitos o thermal shed → wet = 0 (camino legacy intacto).
//
// Honestidad de la v1: la magnitud de pinna NO se modela aquí (no hay
// tabla HRTF medida disponible en esta clase standalone; inventar una
// curva sería fabricar datos). La coloración de pabellón sigue llegando
// por el camino legacy HOA+HRTF con el que ORBE se mezcla, y la
// integración con HrtfManager queda pendiente (ver IMPLEMENTACION_ORBE.md).
// El parallax de v1 es compensación de rotación de cabeza (yaw); el
// parallax por traslación requiere pose de traslación y queda pendiente.
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "NearFieldHeadModel.hpp"
#include "fft_radix2.hpp"

namespace ivanna {

struct OrbeParams {
    float amount = 0.0f;       // 0..1 wet/dry vs legacy; 0 = bit-exacto
    float distanceM = 0.35f;   // 0.15..2.0 m, distancia base ("íntimo")
    float spread = 1.0f;       // 0..1.5, escala del azimut estimado
    float proximityDb = 3.0f;  // 0..6 dB, techo del realce de proximidad
    float diffuse = 0.5f;      // 0..1, cuánto side pasa al render directo
    bool headTracking = true;  // parallax por setHeadPose()
    int bandCount = 24;        // 12 o 24 bandas ERB
    // Radio de cabeza personalizado (m) — Ec. 2 del campo Ψ con la
    // antropometría del oyente (PsiFieldRuntime::headRadiusM). KEMAR
    // 8.75 cm por defecto: sin él, bit-idéntico al modelo original.
    float headRadiusM = nearfield::kHeadRadiusM;
};

class OrbeBinaural {
public:
    static constexpr int kHop = 128;  // == Ivanna::BLOCK_SIZE
    static constexpr int kMaxBands = 24;

    void prepare(float sr);
    void reset() noexcept;
    void setParams(const OrbeParams& p) noexcept;
    void setAmount(float amount) noexcept;
    void setHeadPose(float yawRad) noexcept;
    // Radio de cabeza personalizado (m, 0.05..0.15, KEMAR 0.0875 por
    // defecto). Lo alimenta PsiFieldRuntime (antropometría del oyente);
    // sin él, el render es bit-idéntico al modelo original.
    void setHeadRadiusM(float radiusM) noexcept;
    float headRadiusM() const noexcept;
    void setThermalShed(bool shed) noexcept;
    // process() acepta cualquier n > 0; siempre escribe exactamente n
    // muestras. Con amount = 0 y el fade completado es passthrough
    // bit-exacto (sin tocar los buffers).
    void process(float* l, float* r, int n) noexcept;

    // Diagnóstico / tests.
    int fftSize() const noexcept { return fftSize_; }
    int latencySamples() const noexcept { return fftSize_; }
    float sampleRate() const noexcept { return sampleRate_; }
    float bandAzimuth(int b) const noexcept;
    float bandDistance(int b) const noexcept;
    float bandCoherence(int b) const noexcept;
    float meanCoherence() const noexcept { return meanCoherence_; }
    float appliedAmount() const noexcept { return wet_; }
    bool isActive() const noexcept { return wet_ > 0.0f || blend_ > 0.0f; }

private:
    struct Band {
        int loBin = 0, hiBin = 0;  // rango de bins (0..fftSize/2)
        float centerHz = 1000.0f;
    };

    void rebuildBandTables();
    void runFrame();
    void estimateBands(const float* reL, const float* imL,
                       const float* reR, const float* imR);
    void renderBins(float* reL, float* imL, float* reR, float* imR);
    float thetaFromItd(float tauSeconds, float r) const noexcept;
    float thetaFromIld(float ildDbMeas, float r, float freqHz) const noexcept;
    float distanceFromIld(float ildDbMeas, float theta, float freqHz) const noexcept;

    float sampleRate_ = 48000.0f;
    int fftSize_ = 512;
    bool prepared_ = false;

    std::atomic<float> amount_{0.0f};
    std::atomic<float> distanceM_{0.35f};
    std::atomic<float> spread_{1.0f};
    std::atomic<float> proximityDb_{3.0f};
    std::atomic<float> diffuse_{0.5f};
    std::atomic<float> headRadiusM_{nearfield::kHeadRadiusM};
    std::atomic<bool> headTracking_{true};
    std::atomic<bool> thermalShed_{false};
    std::atomic<float> yawRad_{0.0f};
    std::atomic<int> bandCount_{24};

    // Estado STFT (todo preasignado en prepare()).
    std::unique_ptr<FFTRadix2> fft_;
    std::vector<float> window_;
    std::vector<float> ringL_, ringR_;  // últimos fftSize muestras (historia)
    int ringPos_ = 0;                    // próxima escritura en el anillo
    std::vector<float> pendL_, pendR_;   // acumulador hasta completar un hop
    int pendCount_ = 0;
    std::vector<float> olaL_, olaR_;     // overlap-add
    std::vector<float> wetQL_, wetQR_;   // FIFO de salida: hop renderizado
    std::vector<float> dryQL_, dryQR_;   // FIFO de salida: hop seco alineado
    int qHead_ = 0, qTail_ = 0, qCount_ = 0, qCap_ = 0;
    int frameIndex_ = 0;  // frames corridos desde prepare/reset
    int skipEmit_ = 0;    // frames sin emitir tras un flush (anillo vacío)
    float colaInv_ = 1.0f / 1.5f;  // 1 / ((N/hop)·0.375); se fija en prepare()
    std::vector<float> reL_, imL_, reR_, imR_;  // espectros de trabajo

    // Estimación por banda (suavizada).
    std::vector<Band> bands_;
    std::vector<int> bandOfBin_;  // por bin 0..fftSize-1
    float azimuth_[kMaxBands] = {};
    float distance_[kMaxBands] = {};
    float coherence_[kMaxBands] = {};
    float meanCoherence_ = 1.0f;
    int activeBands_ = 24;

    // Fades (15 ms): wet_ = cantidad aplicada, blend_ = 0 seco en vivo,
    // 1 seco retardado (alineado con el STFT).
    float wet_ = 0.0f;
    float blend_ = 0.0f;
    bool flushPending_ = false;  // vaciar colas una vez al reactivar
    // Re-sondeo de la puerta de coherencia (v2.5.415, bandera 5): el
    // estimador solo se actualiza en runFrame() (solo cuando la etapa está
    // activa), así que una coherencia < 0.3 cerraba la puerta para siempre
    // (latch). Cada kCohProbePeriodBlocks en bypass por coherencia se
    // sondean kCohProbeBlocks seguidos para re-evaluar el estimador.
    static constexpr int kCohProbePeriodBlocks = 150;  // ~1 s entre sondeos
    static constexpr int kCohProbeBlocks = 8;          // ~53 ms por sondeo
    int cohBypassBlocks_ = 0;
    int cohProbeBlocksRemaining_ = 0;
    size_t totalIn_ = 0;   // muestras consumidas
    size_t totalOut_ = 0;  // muestras retardadas ya emitidas
};

}  // namespace ivanna
