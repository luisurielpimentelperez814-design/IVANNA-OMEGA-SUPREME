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
// Ψ VIVA EN LA APP (2026-11-11, directiva de Esse: Ψ funciona sí o sí).
// La app ES el cerebro: calcula los taps Ψ ella misma con PsiCore
// (psi_core.h: Ec. 2 ILD + Ec. 4 η + designFir 64 taps/oído), sin daemon.
// Por defecto ACTIVA (no en bypass): en el primer bloque se diseñan los
// taps con η=0.5 y se abre la puerta; η se alimenta de la coherencia de
// la envolvente 1–8 Hz medida en los propios bloques (misma medida que el
// músculo del HAL: polo rápido ~8 Hz, polo lento ~1 Hz, coherencia =
// E[(rápido−lento)²]/E[rápido²]). Los taps solo se rediseñan cuando η se
// mueve ≥0.02 o cambia la geometría (y ≥0.25 s desde el último diseño):
// el diseño por IFFT es barato pero no va en cada bloque.
// Fail-closed: sin medida fiable (silencio), η se queda; sin Ψ, bypass.
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
#include "psi_core.h"       // PsiCore: el cerebro Ψ vive en la app (Ruta A)

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

    // FIR Ψ (64 taps por oído). La diseña EL CEREBRO DE LA APP (PsiCore,
    // ver abajo); el músculo solo convoluciona. Por defecto ACTIVA: la app
    // diseña los taps en el primer bloque y abre la puerta ella misma.
    // setPsiFir copia 64 taps por oído de una fuente EXTERNA (fusión/daemon
    // futuro): mientras haya taps externos, el auto-diseño se suspende y
    // mandan los externos. setPsiActive abre/cierra la puerta a mano:
    // setPsiActive(false) apaga Ψ y SUPRIME el auto-diseño hasta que se
    // vuelva a llamar setPsiActive(true) o setPsiAuto(true).
    void setPsiFir(const float* firL, const float* firR);
    void setPsiActive(bool active);
    bool psiActive() const {
        return psiActive_.load(std::memory_order_relaxed) != 0;
    }

    // ── Ψ VIVA: la app es el cerebro ──────────────────────────────────
    // Tasa de muestreo del track (Hz) para el diseño del FIR (mapeo
    // 0..Nyquist) y para la medida de envolvente. Default 48000; el
    // reproductor la fija al crear el ASRC (RouteAAsrc.setSampleRate).
    void setSampleRate(float sampleRateHz);
    // Geometría del campo: radio de cabeza (m), distancia (m), azimut
    // (rad, 0 = enfrente). Valores no finitos o <= 0 se ignoran (queda el
    // anterior). Sin setter, el radio se lee del atomic g_orbe_head_radius_m
    // del proceso app si existe (HeadMeasure), si no KEMAR 0.0875 m.
    // Distancia default 0.30 m (punto dulce), azimut default 0.
    void setPsiGeometry(float headRadiusM, float distanceM, float azimuthRad);
    // Auto-diseño + auto-activación de Ψ (default true). false = Ψ apagada
    // y sin auto-diseño (solo se activa a mano con setPsiActive(true)).
    // true = la app retoma el diseño (olvida taps externos).
    void setPsiAuto(bool autoOn);
    // Congela el diseño de los FIR Ψ (los taps quedan fijos, la puerta
    // sigue como esté). Para medir THD+N sin la deriva de ganancia de Γ,
    // o para sostener el campo mientras se ajusta algo. No congela η.
    void setPsiDesignHold(bool hold);
    // Diagnóstico (no en el camino de audio): η actual [0,1] y última
    // coherencia medida [0,1].
    double psiEta() const;
    double psiCoherence() const;
    // Copia los taps Ψ diseñados actualmente (64 por oído). false si aún
    // no se ha diseñado ningún FIR.
    bool psiFirTaps(float* firL, float* firR) const;

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

    // ── Ψ VIVA: el cerebro en la app (Ec. 4 + diseño de FIR) ──────────
    // PsiCore es el núcleo (η, Γ, ILD Ec. 2, designFir). Solo lo toca el
    // hilo de audio (process): los setters solo escriben atómicos.
    ivanna::psi::PsiCore psiCore_;
    std::atomic<float> psiSampleRate_{48000.0f};
    std::atomic<float> psiHeadRadiusSet_{-1.0f};  // <0 = no fijado por setter
    std::atomic<float> psiDistanceSet_{-1.0f};    // <0 = no fijado por setter
    std::atomic<float> psiAzimuthSet_{0.0f};     // 0 = enfrente
    std::atomic<int> psiAuto_{1};
    std::atomic<int> psiManualOff_{0};
    std::atomic<int> psiExternalTaps_{0};
    std::atomic<int> psiDesignHold_{0};
    // Envolvente/coherencia 1–8 Hz (AC-acoplada; ver .cpp: la versión
    // DC-acoplada del HAL no pasa de ~0.25 y η nunca sube).
    double envFast_ = 0.0, envSlow_ = 0.0;
    double envBandE_ = 0.0, envAcE_ = 0.0;
    int envN_ = 0;
    double envAccumSec_ = 0.0;
    double psiTimeSec_ = 0.0;
    double lastCoherence_ = 0.5;
    // Caché de diseño: solo se rediseña cuando η se mueve ≥0.02 o cambia
    // la geometría (y ≥0.25 s desde el último diseño).
    bool psiFirsDesigned_ = false;
    double psiEtaLastDesign_ = -1.0;
    float psiHeadLast_ = -1.0f, psiDistLast_ = -1.0f, psiAzimLast_ = -1.0f;
    double psiLastDesignSec_ = -1e9;

    void updatePsiEnvelope(const float* in, int inFrames);
    void maybeRedesignPsi(bool force);
    float resolvePsiHeadRadius() const;

    std::atomic<uint64_t> totalIn_{0};
    std::atomic<uint64_t> totalOut_{0};

    static double ratioToPpm(double ratio) { return (ratio - 1.0) * 1.0e6; }
};

}  // namespace ivanna::routea
