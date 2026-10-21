// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#pragma once

/*
 * ============================================================================
 * IVANNA OMEGA SUPREME — PredictiveAudioEngine (Módulo 1, Fase Cero)
 * Pronosticador LPC de horizonte corto (5–10 ms) con validación en lazo cerrado.
 *
 * QUÉ ES (y qué no)
 * -----------------
 * Extrapola la forma de onda de los próximos 5–10 ms a partir de las últimas
 * muestras, con predicción lineal (método de Burg, orden ≤ 32) y recursión
 * en punto fijo entero: coeficientes int32 con formato Q dinámico, datos con
 * 8 bits fraccionarios en la recursión, acumulador int64 (bit-exacto entre
 * dispositivos, sin denormales). Se probó primero con coeficientes int16: los
 * coeficientes de orden alto son mal condicionados y esa cuantización dejaba el
 * pronóstico peor que predecir silencio (medido), por eso int32. No es una red neuronal: es el predictor clásico que mejor
 * se comporta a este horizonte (el PhaseKalman3 del repo, de aceleración
 * constante, diverge a las pocas muestras; ver phase_oracle_kalman.hpp).
 *
 * LÍMITES FÍSICOS, DOCUMENTADOS A PROPÓSITO
 * -----------------------------------------
 *  - Un pronóstico NO elimina latencia: no se puede conocer una muestra que
 *    aún no existe. Solo es fiable en señal PREDECIBLE (tonos, sostenidos,
 *    graves periódicos). En ruido o en el instante de un ataque percusivo el
 *    error es ≥ 100 %: el motor lo MIDE y lo reporta (confidence → 0,
 *    valid = false) en vez de aparentar que acierta.
 *  - Un transitorio no se anticipa: se DETECTA en la misma muestra en que
 *    ocurre, porque el residuo de predicción salta (detector de onset por
 *    residuo, sin lookahead y sin latencia añadida).
 *  - Este módulo es SOLO ANÁLISIS: no modifica el audio y no puede mover
 *    membranas. Entrega pico/slew/frecuencia previstos y confianza para que
 *    otra etapa (p. ej. limitador, pre-armado de ganancia) decida qué hacer.
 *
 * VALIDACIÓN EN LAZO CERRADO
 * --------------------------
 * Cada pronóstico se guarda y se compara con las muestras reales conforme
 * llegan. NMSE = Σ(error²)/Σ(señal²) (1.0 = no mejor que predecir silencio),
 * separado en horizonte cercano (< H/2) y lejano (≥ H/2).
 * confidence = clamp(1 − NMSE_lejano, 0, 1). Arranque en frío: confidence = 0
 * hasta que el primer pronóstico haya sido verificado.
 *
 * TIEMPO REAL
 * -----------
 * Sin malloc, sin locks, sin I/O, sin excepciones, sin STL dinámica: todos los
 * buffers son miembros de tamaño fijo. Costo acotado: el re-ajuste Burg corre
 * cada `hop` muestras (≈ order·analysisLen·6 flops por ajuste).
 * Un hilo productor (audio). Los lectores deben estar en el mismo hilo o
 * copiar el frame (struct trivial).
 * ============================================================================
 */

#include <array>
#include <cstdint>

namespace ivanna {

struct PredictiveConfig {
    int   order          = 32;      // 4..kMaxOrder
    int   horizon        = 240;     // muestras pronosticadas (240 = 5 ms @ 48 kHz); ≤ kMaxHorizon
    int   hop            = 120;     // re-ajuste cada `hop` muestras (se acota a [horizon/kSlots, horizon])
    int   analysisLen    = 1024;    // muestras que ve Burg (64..kWindow); más ventana = mejor resolución en graves
    float bandwidthGamma = 0.9998f; // expansión de ancho de banda (estabilidad); ↓ = más amortiguado
    float onsetRatio     = 12.0f;   // residuo rápido/lento (potencia) que marca un onset
    float minConfidence  = 0.5f;    // umbral para frame.valid
    bool  quantized      = true;    // true: recursión en punto fijo entero; false: referencia en double
};

struct PredictiveFrame {
    bool     valid              = false;  // pronóstico fiable (confidence >= minConfidence)
    float    confidence         = 0.f;    // 1 − NMSE_lejano, [0,1]
    float    nmseNear           = 1.f;    // error normalizado, edad < H/2
    float    nmseFar            = 1.f;    // error normalizado, edad ≥ H/2
    float    predictionGainDb   = 0.f;    // 10·log10(energía / error) del ajuste Burg
    float    predictedPeak      = 0.f;    // max |pronóstico| (escala completa = 1.0)
    float    predictedSlewPerMs = 0.f;    // max |Δmuestra| · fs / 1000 (escala completa por ms)
    float    zeroCrossHz        = 0.f;    // tasa de cruces por cero/2 del pronóstico; ≈ fundamental SOLO si la señal es casi senoidal (0 = < 3 cruces)
    bool     transient          = false;  // hubo onset durante el último hop
    uint32_t onsetCount         = 0;
    int64_t  lastOnsetSample    = -1;     // índice absoluto de muestra, -1 si ninguno
    uint32_t forecastsGenerated = 0;
    uint32_t forecastsRejected  = 0;      // descartados por silencio, inestabilidad o no-finitud
};

class PredictiveAudioEngine {
public:
    static constexpr int kMaxOrder   = 32;
    static constexpr int kMaxHorizon = 480;   // 10 ms @ 48 kHz
    static constexpr int kWindow     = 1024;  // potencia de 2 (se usa como máscara)
    static constexpr int kSlots      = 4;     // pronósticos simultáneos en validación

    // No RT (llamar antes de procesar). Sin asignación de memoria.
    void prepare(float sampleRate, const PredictiveConfig& cfg = PredictiveConfig{}) noexcept;
    void reset() noexcept;

    // RT-safe. n puede ser cualquier tamaño de bloque.
    void processMono(const float* x, int n) noexcept;
    void processStereo(const float* l, const float* r, int n) noexcept;   // mono = (L+R)/2

    const PredictiveFrame& frame() const noexcept { return frame_; }
    // Último pronóstico, `horizon()` muestras float [-1,1]; el índice 0 es la
    // muestra siguiente a la última procesada en el momento del ajuste.
    const float* forecast() const noexcept { return forecastOut_.data(); }
    int      horizon() const noexcept { return horizon_; }
    float    sampleRate() const noexcept { return sr_; }
    uint64_t samplesProcessed() const noexcept { return total_; }
    // Acceso inmediato (sin esperar al siguiente hop).
    uint32_t onsetCount() const noexcept { return onsetCount_; }
    int64_t  lastOnsetSample() const noexcept { return lastOnset_; }

private:
    struct Slot {
        std::array<int16_t, kMaxHorizon> fc{};
        uint64_t start  = 0;
        bool     active = false;
    };

    void pushSample(float x) noexcept;
    void updateModel() noexcept;
    void clearModel() noexcept;
    int  predictOne() const noexcept;              // predicción 1 paso con el modelo vigente
    void storeForecast(const int16_t* fc) noexcept;

    // ── Configuración ───────────────────────────────────────────────────
    PredictiveConfig cfg_{};
    float sr_       = 48000.f;
    int   order_    = 32;
    int   horizon_  = 240;
    int   hop_      = 120;
    int   analysis_ = 1024;
    double alphaFast_ = 0.0, alphaSlow_ = 0.0;   // EMA del residuo: τ = 1 ms / 100 ms
    int    refractory_ = 960;                    // 20 ms entre onsets

    // ── Historia ────────────────────────────────────────────────────────
    std::array<int16_t, kWindow> ring_{};
    int      wpos_  = 0;
    uint64_t total_ = 0;
    int      sinceHop_ = 0;

    // ── Modelo vigente ──────────────────────────────────────────────────
    bool    haveModel_ = false;
    int     modelOrder_ = 0;
    int     qshift_ = 0;
    std::array<int32_t, kMaxOrder> q_{};
    std::array<double,  kMaxOrder> c_{};   // referencia double (modo no cuantizado)

    // ── Validación en lazo cerrado ──────────────────────────────────────
    std::array<Slot, kSlots> slots_{};
    int    slotNext_ = 0;
    double errNear_ = 0, sigNear_ = 0, errFar_ = 0, sigFar_ = 0;
    uint32_t cntNear_ = 0, cntFar_ = 0;
    double nmseNear_ = 1.0, nmseFar_ = 1.0;

    // ── Detector de onset por residuo ───────────────────────────────────
    double envFast_ = 0, envSlow_ = 0;
    int    sinceOnset_ = 1 << 30;
    int    onsetsThisHop_ = 0;
    uint32_t onsetCount_ = 0;
    int64_t  lastOnset_ = -1;

    // ── Salida ──────────────────────────────────────────────────────────
    PredictiveFrame frame_{};
    std::array<float, kMaxHorizon> forecastOut_{};

    // ── Scratch (Burg) ──────────────────────────────────────────────────
    std::array<double, kWindow> f_{};
    std::array<double, kWindow> b_{};
};

} // namespace ivanna
