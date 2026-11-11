// ivanna_route_a_asrc.h — ASRC MAGISTRAL para Ruta A (proceso app, no HAL).
//
// Reutiliza el núcleo v6 del HAL (FIR 319 taps minimum-phase + interpolación
// cúbica Catmull-Rom entre las fases, FIR Ψ 64 taps por oído), adaptado
// a la cadena Ruta A: E/S float (la cadena DSP es PCM_FLOAT), streaming por
// bloques con estado fraccionario persistente, y ratio alimentado por el PLL
// existente (AudioTrackBufferPll) en vez del cerebro del HAL.
//
// REGLA #1 DE ESSE (2026-11-11): nada básico, magistral, omnipotente,
// riguroso, literal. Por eso esto NO es el remuestreador Kotlin de 64 taps:
// es el FIR 319 taps + Catmull-Rom del v6, con la FIR Ψ del campo, en C++
// nativo con NEON.
//
// Seguridad en tiempo real:
//  - Sin allocs después de crear el objeto (todos los buffers son miembros
//    de tamaño fijo). Sin locks en el hilo de audio: el ratio y la puerta Ψ
//    se comunican por atómicos (relaxed).
//  - Fail-closed: ratio inválido (NaN, fuera de [0.999, 1.001]) → 1.0.
//    Si el FIR Ψ no está activo, es bypass total (cero coste).
//  - Si este nativo no carga, la app sigue sonando con el remuestreador
//    Kotlin (fallback, ver RouteAAsrc.kt): el audio nunca se rompe.
//
// Límites honestos (ver ASRC_RUTA_A_MAGISTRAL_2026-11-11.md):
//  - Esto corrige la deriva del track PROPIO de la app (su AudioTrack).
//    NO disciplina el DAC físico, ni el HAL, ni otra app. AudioFlinger
//    remuestrea a 48 kHz antes del HAL; la corrección es sobre el track.
//  - El FIR tiene retardo de grupo constante (~32 muestras, ~0.67 ms a
//    48 kHz, medido en host a 1 kHz): es latencia, no pérdida, y es
//    idéntico con ratio 1.0 o no.
#pragma once

#include <atomic>
#include <cstdint>

#include "fir_v5_coeffs.h"  // FIR_V5_PHASES / FIR_V5_TAPS (tabla única del HAL)

namespace ivanna::routea {

// Dimensiones de la tabla FIR (única fuente: fir_v5_coeffs.h, generada por
// gen_fir_v5.py). 2048 fases y 64 taps de interpolador desde 2026-11-11
// (antes 1024 fases / 32 taps): el interpolador de 32 taps dejaba un rizo
// de ganancia con frac que limitaba el THD+N a -86.6 dB; con 64 taps el
// THD+N a +100 ppm es -95.0 dB (medido en host). Retardo de grupo
// ~32 muestras (~0.67 ms). Passband plano a 20 kHz, stopband -156 dB.
inline constexpr int kAsrcTaps = FIR_V5_TAPS;      // 319
inline constexpr int kAsrcPhases = FIR_V5_PHASES;  // 2048
inline constexpr int kAsrcHistFrames = 2 * kAsrcTaps;  // ventana + espejo
inline constexpr int kPsiTaps = 64;

// Ratio permitido: ±1000 ppm (igual que el v6 del HAL; el PLL de Ruta A
// ya limita a ±500 ppm, así que este clamp es la segunda capa).
inline constexpr double kRatioMin = 1.0 - 1000e-6;
inline constexpr double kRatioMax = 1.0 + 1000e-6;

class RouteAAsrc {
public:
    RouteAAsrc();
    ~RouteAAsrc() = default;
    RouteAAsrc(const RouteAAsrc&) = delete;
    RouteAAsrc& operator=(const RouteAAsrc&) = delete;

    // Consume `inFrames` (estéreo intercalado float L/R) y produce la
    // salida remuestreada en `out` (capacidad `outCap` frames).
    // Devuelve los frames producidos (0 si no hay nada que producir).
    // `outCap` debe ser >= inFrames * 1.002 + 16 (el ratio máximo es
    // 1.001 y el carry fraccionario puede aportar ±1 frame por bloque).
    // El estado (historia FIR, posición fraccionaria, historia Ψ) persiste
    // entre bloques: la salida es continua, sin discontinuidad de fase.
    int process(const float* in, int inFrames, float* out, int outCap);

    // Corrección de reloj del PLL (ppm). Positivo = producir más rápido
    // (misma convención que StereoAudioResampler.setClockCorrectionPpm y
    // que AudioTrackBufferPll: deriva positiva = el consumidor va más
    // rápido). Clampeado a ±1000 ppm; NaN/no-finito → 0 (ratio 1.0).
    // Llamado desde el hilo del reproductor (no el de audio) o desde el
    // poll del PLL: atómico, sin locks.
    void setClockCorrectionPpm(double ppm);
    double clockCorrectionPpm() const {
        return ratioToPpm(ratio_.load(std::memory_order_relaxed));
    }

    // FIR Ψ (64 taps por oído). La diseña el cerebro; el músculo solo
    // convoluciona. Por defecto INACTIVA (bypass). setPsiFir copia los
    // 64 taps por oído; setPsiActive abre/cierra la puerta. Sin cerebro
    // vivo, Ψ permanece en bypass (misma puerta que la fusión).
    void setPsiFir(const float* firL, const float* firR);
    void setPsiActive(bool active);
    bool psiActive() const {
        return psiActive_.load(std::memory_order_relaxed) != 0;
    }

    // Reinicia el estado (historia, posición, Ψ): usar en seek, cambio
    // de ruta (reroute) o cambio de pista. El primer bloque tras reset
    // arranca desde silencio (fade-in del FIR, ~6 ms, sin click).
    void reset();

    // Estadística de diagnóstico (no en el camino de audio).
    uint64_t totalInFrames() const {
        return totalIn_.load(std::memory_order_relaxed);
    }
    uint64_t totalOutFrames() const {
        return totalOut_.load(std::memory_order_relaxed);
    }

private:
    void firCoeffsForFrac(double frac);
    void histPush(float l, float r);
    void applyPsiFir(float* buf, int frames);

    // Ratio (salida/entrada). 1.0 = sin corrección.
    std::atomic<double> ratio_{1.0};

    // Posición de entrada (frames, relativa al bloque actual) de la
    // próxima muestra de salida. Carry entre bloques, en [-1, 0].
    double pos_ = 0.0;

    // Historia FIR: buffers circulares espejados (2*kAsrcTaps frames por canal).
    // histW = índice de escritura (0..286). histFilled = posición de
    // entrada hasta la que se ha llenado la historia en este bloque.
    float histL_[kAsrcHistFrames];
    float histR_[kAsrcHistFrames];
    int histW_ = 0;
    int32_t histFilled_ = 0;
    float coeff_[kAsrcTaps];  // coeficientes interpolados (temp por muestra)

    // FIR Ψ: delay line de 64 taps por oído + coeficientes del cerebro.
    float psiHistL_[kPsiTaps];
    float psiHistR_[kPsiTaps];
    int psiHistW_ = 0;
    float psiFirL_[kPsiTaps];
    float psiFirR_[kPsiTaps];
    std::atomic<int> psiActive_{0};

    std::atomic<uint64_t> totalIn_{0};
    std::atomic<uint64_t> totalOut_{0};

    static double ratioToPpm(double ratio) { return (ratio - 1.0) * 1.0e6; }
};

}  // namespace ivanna::routea
