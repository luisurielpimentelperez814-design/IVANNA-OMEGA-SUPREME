#include <algorithm>
#include "EvolutionaryEQ.hpp"
#include "IvannaFusionCore.hpp"
#include "spatial/EliteTargetCurves.hpp"
#include <cstddef>
#include <cmath>
#include <random>

namespace Ivanna {

// ─────────────────────────────────────────────────────────────────────────────
// Curva objetivo: Harman AE/OE 2018 (EliteTargetCurves, ÚNICA fuente de
// verdad) escalada ×0.5.
//
// REPARACIÓN v2.5.369: la curva paramétrica anterior ("presencia": −2 dB en
// graves, +3 dB en 13 kHz, +1.5 dB en 20.6 kHz) CONTRADICE el target élite
// del producto y no medía nada por debajo de 458 Hz — los graves quedaban en
// −2.6 dB por arrastre del regularizador, sin objetivo. Además Φ (amount 0.5)
// ya corrige hacia Harman: que el EvoEq apunte a otra curva es incoherente
// (ambas etapas tiraban en direcciones opuestas). Ahora el objetivo es
// Harman 2018 (13 anclajes, 20 Hz–16 kHz) ×0.5: complementa el amount 0.5
// de Φ, así que ambas etapas suman Harman completo en defaults. El módulo es
// libre de apartarse de ella cuando otro frente imponga su propio sesgo.
// ─────────────────────────────────────────────────────────────────────────────
namespace {
constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

// Escala del objetivo Harman: 0.5 complementa Φ (amount 0.5) → Harman total.
constexpr float kHarmanScale = 0.5f;
// Penalizacion de suavidad: 0.5 dominaba el error espectral (20.25 para
// identidad) y empujaba el optimizador a una respuesta casi plana. 0.01
// conserva continuidad entre bandas sin cancelar la curva objetivo.
constexpr float kSmoothnessWeight = 0.002f;
// Tope de seguridad de magnitud en frecuencia: +6 dB. El pico Harman ×0.5
// es +4.3 dB (1.64×), así que el tope no se alcanza en convergencia; solo
// protege contra un boost extremo si el optimizador se desvía.
constexpr float kMaxMagnitude = 2.0f;
// Frecuencia de referencia Harman (1 kHz): el FIR se ancla en |H|=1.0 ahí.
constexpr float kRefFreqHz = 1000.0f;
constexpr float kRefSampleRateHz = 48000.0f;

// Piso del objetivo en dB: el FIR de 256 taps con ventana Blackman-Harris
// NO puede realizar un corte profundo en la octava superior (medido: a
// 16 kHz el mejor corte alcanzable es ~−1.2 dB; la respuesta por encima de
// 11 kHz queda casi plana con ripple ±0.8 dB aunque el genoma apunte a
// −4 dB). El objetivo Harman ×0.5 a 16 kHz es −4.15 dB: inalcanzable y
// dominaba el fitness (8.6 de 11.5) sin que el optimizador pudiera
// arreglarlo. Se fija un piso de −2.0 dB (el corte más profundo que el
// diseño realiza de forma fiable en el top-octave). Es una limitación
// MEDIDA del diseño FIR, no un ajuste oculto: el único punto afectado es
// 16 kHz (−4.15 → −2.0); el resto de Harman ×0.5 queda intacto.
// Anclajes Harman 2018 (Hz). Los dB se leen de EliteTargetCurves (no se
// duplican): eliteTargetDbAt(Harman2018, f) × kHarmanScale.
constexpr int   kNumTargets = 13;
constexpr float kTargetDbFloor = -2.0f;
constexpr float kTargetFreqHz[kNumTargets] = {
    20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f,
    3150.0f, 4000.0f, 6300.0f, 8000.0f, 10000.0f, 16000.0f
};

// Ventana Blackman-Harris (4 términos) en x ∈ [−1, 1]. Con esta
// parametrización centrada los signos son todos + (equivale a la forma
// estándar a0 − a1·cos(2πn/N) + a2·cos(4πn/N) − a3·cos(6πn/N)): w(±1) ≈ 0,
// w(0) = 1. Se extrae para compartirla entre designTaps y la matriz de
// fitness (antes estaba duplicada/inline solo en designTaps).
inline float bhWindow(float x) {
    return 0.35875f
         + 0.48829f * std::cos(kPi * x)
         + 0.14128f * std::cos(2.0f * kPi * x)
         + 0.01168f * std::cos(3.0f * kPi * x);
}

inline float db2lin(float db) { return std::pow(10.0f, db / 20.0f); }

// Diseña el FIR (taps) desde el genoma espectral (BANDS_512 bandas, en dB).
//
// Garantía estructural: con genoma neutral (todas las bandas en 0 dB) el
// resultado es EXACTAMENTE el delta δ[t-128] ⇒ respuesta identidad plana.
// Cada banda desviada añade una sinusoide enventanada a la frecuencia
// normalizada que le corresponde (wn = π·(b+0.5)/BANDS_512). Es un diseño
// por superposición de bases: la respuesta en magnitud del resultado refleja
// de forma monótona la desviación del genoma en cada banda.
//
// NOTA (asimetría de medio-sample, intencional): el centro simétrico es
// 127.5 pero el delta está en t=128. Eso rompe la simetría exacta (el FIR
// no es Tipo-II puro) y es lo que permite respuesta NO nula en Nyquist.
// Un FIR Tipo-II simétrico (256 taps) fuerza un cero en Nyquist y no podría
// representar el objetivo en la octava superior. No "corregir" a simétrico.
void designTaps(const float* genome, float* taps) {
    const float center = static_cast<float>(FIR_TAPS - 1) * 0.5f;

    for (size_t t = 0; t < FIR_TAPS; ++t) {
        const float x = (static_cast<float>(t) - center) / center; // -1..1
        const float w = bhWindow(x);

        float h = (t == FIR_TAPS / 2) ? 1.0f : 0.0f; // δ ⇒ identidad exacta

        for (size_t b = 0; b < BANDS_512; ++b) {
            const float dev = db2lin(genome[b]) - 1.0f; // 0 si neutral
            if (std::fabs(dev) < 1e-5f) continue;
            const float wn = kPi * (static_cast<float>(b) + 0.5f)
                                  / static_cast<float>(BANDS_512);
            h += dev * w * std::cos(wn * (static_cast<float>(t) - center))
                 * (2.0f / static_cast<float>(FIR_TAPS));
        }
        taps[t] = h;
    }
}

// ── Matriz de fitness precomputada (exacta, por linealidad de la DTFT) ──
//
// El fitness solo necesita |H(ω)| en los 13 objetivos. Como taps[t] es
// LINEAL en dev[b] = 10^(genoma[b]/20) − 1 (más el delta), la DTFT en cada
// objetivo es:  H(ωk) = e^(−j·ωk·128) + Σ_b dev[b]·M[b][k],  con
// M[b][k] = Σ_t base[t][b]·e^(−j·ωk·t)  (base = ventana × cos × 2/256).
// Precomputar M (una vez, ~10 ms) sustituye 132k cos() por evaluación por
// ~13k MACs: la calibración pasa de ~967 ms a ~10 ms en host. Es la MISMA
// matemática que designTaps + magnitudeAt (mismo resultado salvo redondeo);
// el test EvoEqFastFitnessMatchesReference lo verifica contra el cálculo
// directo por diseño de taps.
struct FitnessTables {
    float targetWn[kNumTargets];
    float targetDb[kNumTargets];
    float deltaRe[kNumTargets];
    float deltaIm[kNumTargets];
    float mRe[BANDS_512][kNumTargets];
    float mIm[BANDS_512][kNumTargets];
};

const FitnessTables& fitnessTables() {
    static const FitnessTables t = [] {
        FitnessTables ft{};
        const float center = static_cast<float>(FIR_TAPS - 1) * 0.5f;
        for (int k = 0; k < kNumTargets; ++k) {
            const float wn = kTwoPi * kTargetFreqHz[k] / kRefSampleRateHz;
            ft.targetWn[k] = wn;
            ft.targetDb[k] = std::fmax(
                ivanna::spatial::eliteTargetDbAt(
                    ivanna::spatial::EliteTargetKind::Harman2018,
                    kTargetFreqHz[k]) * kHarmanScale,
                kTargetDbFloor);
            ft.deltaRe[k] = std::cos(wn * static_cast<float>(FIR_TAPS / 2));
            ft.deltaIm[k] = -std::sin(wn * static_cast<float>(FIR_TAPS / 2));
        }
        // Precomputar cos/sin del objetivo por tap (evita trig en el bucle).
        float tCos[kNumTargets][FIR_TAPS];
        float tSin[kNumTargets][FIR_TAPS];
        for (int k = 0; k < kNumTargets; ++k)
            for (size_t t = 0; t < FIR_TAPS; ++t) {
                tCos[k][t] = std::cos(ft.targetWn[k] * static_cast<float>(t));
                tSin[k][t] = std::sin(ft.targetWn[k] * static_cast<float>(t));
            }
        for (size_t t = 0; t < FIR_TAPS; ++t) {
            const float x = (static_cast<float>(t) - center) / center;
            const float w = bhWindow(x);
            for (size_t b = 0; b < BANDS_512; ++b) {
                const float wnB = kPi * (static_cast<float>(b) + 0.5f)
                                       / static_cast<float>(BANDS_512);
                const float base = w * std::cos(wnB * (static_cast<float>(t) - center))
                                 * (2.0f / static_cast<float>(FIR_TAPS));
                for (int k = 0; k < kNumTargets; ++k) {
                    ft.mRe[b][k] += base * tCos[k][t];
                    ft.mIm[b][k] += base * (-tSin[k][t]);
                }
            }
        }
        return ft;
    }();
    return t;
}

// Adaptación de paso POR ÉXITO (regla 1/5). Si el fitness del genoma medio
// mejoró respecto a la generación anterior, el paso crece (×1.05, hasta
// 1.0 dB); si no, se encoge (×0.95, hasta 0.05 dB) para afinar. Es robusta
// porque solo depende de si se progresa, no de la distribución del paso.
//
// (Se probó CSA —el evolution path acumulaba el paso normalizado— y se
// descartó por MEDICIÓN: el paso medio es ~10× menor que una muestra N(0,1)
// por la recombinación 0.5 y la mutación uniforme, así que ||p|| << χ=22.6
// y CSA colapsaba σ al mínimo en 8 generaciones, dejando la respuesta casi
// plana. El evolution path era código muerto y se eliminó.)
constexpr float kStepUp = 1.05f;
constexpr float kStepDown = 0.95f;
constexpr float kStepMin = 0.05f;
constexpr float kStepMax = 0.5f; // tope: mutaciones de ±1 dB eran demasiado gruesas para afinar

} // namespace

float EvolutionaryEQ::fastRand() {
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    return dist(m_rng);
}

EvolutionaryEQ::EvolutionaryEQ() {
    for (size_t i = 0; i < FIR_TAPS; ++i) {
        m_firCoeffsL[i] = (i == FIR_TAPS / 2) ? 1.0f : 0.0f;
        m_firCoeffsR[i] = (i == FIR_TAPS / 2) ? 1.0f : 0.0f;
    }

    for (size_t i = 0; i < BLOCK_SIZE + FIR_TAPS; ++i) {
        m_histL[i] = 0.0f;
        m_histR[i] = 0.0f;
    }

    for (size_t i = 0; i < BANDS_512; ++i) {
        // Arranque NEUTRO: genoma en 0 dB ⇒ filtro identidad exacto.
        m_meanGenome[i] = 0.0f;
    }

    rebuildFilterFromGenome(); // deja el FIR en identidad (genoma 0 dB)
    m_ready = false;           // …pero NO habilitado: espera calibración real
}

// Respuesta en magnitud |H(ω)| del FIR `coeffs` en frecuencia normalizada wn.
float EvolutionaryEQ::magnitudeAt(const float* coeffs, float wn) const {
    float re = 0.0f;
    float im = 0.0f;
    for (size_t t = 0; t < FIR_TAPS; ++t) {
        const float a = wn * static_cast<float>(t);
        re += coeffs[t] * std::cos(a);
        im -= coeffs[t] * std::sin(a);
    }
    return std::sqrt(re * re + im * im);
}

// Fitness REAL: la respuesta en magnitud del FIR diseñado desde `genome`
// debe acercarse a Harman 2018 ×0.5 (13 anclajes, 20 Hz–16 kHz). Un
// regularizador de suavidad de peso bajo evita filtros peine; un guard de
// pico evita el filtro identidad trivial y cualquier boost que sature la
// cadena.
//
// Implementación vía matriz precomputada (linealidad de la DTFT): mismo
// resultado que designTaps + magnitudeAt, ~10× más rápido. El pico NO se
// penaliza aquí: penalizarlo (o normalizarlo) lleva el óptimo al identidad
// trivial. El pico se controla solo como auto-verificación de calibrate() +
// normalización del FIR aplicado —nunca en el criterio de optimización—,
// así el fitness puede explorar forma espectral real.
float EvolutionaryEQ::calculateFitness(const float* genome) {
    const FitnessTables& T = fitnessTables();

    float dev[BANDS_512];
    for (size_t b = 0; b < BANDS_512; ++b) dev[b] = db2lin(genome[b]) - 1.0f;

    float err = 0.0f;
    for (int k = 0; k < kNumTargets; ++k) {
        float re = T.deltaRe[k];
        float im = T.deltaIm[k];
        for (size_t b = 0; b < BANDS_512; ++b) {
            re += dev[b] * T.mRe[b][k];
            im += dev[b] * T.mIm[b][k];
        }
        const float mag = std::sqrt(re * re + im * im);
        const float db  = 20.0f * std::log10(std::fmax(mag, 1e-6f));
        const float d   = db - T.targetDb[k];
        err += d * d;
    }

    // Regularizador (NO criterio principal): peso 0.01; mantiene continuidad
    // sin dominar el error espectral ni colapsar el filtro a identidad.
    float smooth = 0.0f;
    for (size_t i = 1; i < BANDS_512; ++i) {
        const float diff = genome[i] - genome[i - 1];
        smooth += diff * diff;
    }
    err += kSmoothnessWeight * smooth;

    return -err;
}

float EvolutionaryEQ::currentFitness() const {
    return const_cast<EvolutionaryEQ*>(this)->calculateFitness(m_meanGenome);
}

void EvolutionaryEQ::rebuildFilterFromGenome() {
    float taps[FIR_TAPS];
    designTaps(m_meanGenome, taps);

    // 1. ANCLA en la referencia Harman (1 kHz): |H(1 kHz)| = 1.0. Así el
    //    EQ preserva la FORMA Harman relativa a 1 kHz y el nivel global en
    //    unidad. La normalización anterior (pico ≤ +3 dB) habría escalado
    //    todo −5.6 dB con el pico Harman (+8.6 dB): forma correcta pero
    //    nivel hundido. El ancla es casi no-op en convergencia (|H(1kHz)|
    //    ≈ 1.0 por el objetivo) y protege contra deriva de nivel.
    const float wnRef = kTwoPi * kRefFreqHz / kRefSampleRateHz;
    const float magRef = magnitudeAt(taps, wnRef);
    float norm = (std::isfinite(magRef) && magRef > 1e-6f) ? (1.0f / magRef) : 1.0f;
    // Exactitud: con genoma neutral |H(1 kHz)| = 1.0 salvo redondeo float
    // (cos²+sin² ≈ 1 ± ε) y la norma quedaría 1.0 ± ε, rompiendo la
    // identidad EXACTA del delta. Si está a <1e-6 de 1.0, se fija en 1.0f.
    if (std::fabs(norm - 1.0f) < 1e-6f) norm = 1.0f;
    for (size_t t = 0; t < FIR_TAPS; ++t) taps[t] *= norm;

    // 2. TOPES DE SEGURIDAD sobre el FIR ya anclado: magnitud máxima en
    //    frecuencia (rejilla densa, no solo los 13 objetivos) ≤ +6 dB y
    //    pico de tap ≤ 1.5. Si un tope se dispara se escala el FIR entero
    //    (se preserva la forma, se baja el nivel) — nunca se satura.
    //    El tope de tap es 1.5 (no 1.0): el FIR Harman natural tiene pico
    //    ~1.05 y caparlo a 1.0 hundía el nivel ~0.45 dB y rompía el ancla
    //    de 1 kHz. El tope real de seguridad es la magnitud (2.0) + el
    //    clamp de salida; el de tap solo caza FIRs patológicos.
    float maxMag = 0.0f;
    for (int i = 0; i <= 64; ++i) {
        const float wn = kPi * static_cast<float>(i) / 64.0f;
        maxMag = std::fmax(maxMag, magnitudeAt(taps, wn));
    }
    float peak = 0.0f;
    for (size_t t = 0; t < FIR_TAPS; ++t) peak = std::fmax(peak, std::fabs(taps[t]));
    float cap = 1.0f;
    if (std::isfinite(maxMag) && maxMag > kMaxMagnitude) cap = std::fmin(cap, kMaxMagnitude / maxMag);
    if (std::isfinite(peak) && peak > 1.5f) cap = std::fmin(cap, 1.5f / peak);
    if (cap < 1.0f) {
        for (size_t t = 0; t < FIR_TAPS; ++t) taps[t] *= cap;
        norm *= cap;
    }

    for (size_t t = 0; t < FIR_TAPS; ++t) {
        m_firCoeffsL[t] = taps[t];
        m_firCoeffsR[t] = taps[t]; // diseño simétrico L/R
    }
    m_normalization = norm;
}

void EvolutionaryEQ::updateLM_CMA_ES() {
    // Optimización evolutiva. Cada generación reproduce una población λ a
    // partir del genoma medio, la evalúa con calculateFitness() (criterio de
    // AUDIO real, ver arriba) y avanza la media hacia el mejor individuo.
    // Al terminar, el genoma se materializa en el FIR aplicado.
    //
    // Esto NO habilita la aplicación (m_ready sigue como esté): la activación
    // es responsabilidad de calibrate(), tras auto-verificación.
    //
    // El FIR NO se reconstruye aquí (antes se hacía en cada generación: 48×
    // designTaps con cos). El optimizador trabaja sobre el GENOMA y el
    // fitness no necesita los taps (matriz precomputada); el FIR se
    // materializa UNA vez al final de calibrate(). Mismo resultado, ~50×
    // menos trabajo por generación.
    constexpr size_t lambda = 16;
    float population[lambda][BANDS_512];
    float fitness[lambda];

    // El genoma actual ocupa el primer slot como elite. Antes los ocho
    // candidatos eran mutaciones: el comentario afirmaba que el promedio
    // actual estaba en la poblacion, pero el codigo no lo incluia.
    // Con el elite explicito, el proceso conserva una referencia conocida
    // y solo se desplaza cuando las mutaciones mejoran el fitness real.
    for (size_t p = 0; p < lambda; ++p) {
        for (size_t i = 0; i < BANDS_512; ++i) {
            population[p][i] = (p == 0)
                ? m_meanGenome[i]
                : m_meanGenome[i] + m_stepSize * fastRand();
        }
        fitness[p] = calculateFitness(population[p]);
    }

    // Selección por promedio ponderado por fitness (softmax estable) sobre la
    // población, en el dominio dB del genoma. La media lineal de ±1 aleatorios
    // es ~0 (el genoma nunca salía del punto fijo en 0 dB —bug atrapado por
    // test_evolutionary_eq), así que se pondera cada individuo por su fitness
    // relativo: los genomas cuya respuesta medida es más cercana al objetivo
    // dominan la siguiente media. Como el genoma medio SIEMPRE está en la
    // población evaluada y el softmax le da peso máximo al mejor, la media
    // converge de forma monótona hacia el objetivo (verificable por fitness).
    float next[BANDS_512];
    {
        // Ponderación softmax estable sobre fitness (menos negativo = mejor).
        // Temperatura 0.5 dB² (antes 1.0): selección más nítida para que los
        // mejores individuos dominen la media y converja más rápido.
        float wmax = fitness[0];
        for (size_t p = 1; p < lambda; ++p) if (fitness[p] > wmax) wmax = fitness[p];
        float wsum = 0.0f;
        float wacc[lambda];
        for (size_t p = 0; p < lambda; ++p) {
            wacc[p] = std::exp((fitness[p] - wmax) / 0.5f); // escala térmica 0.5 dB
            wsum += wacc[p];
        }
        for (size_t i = 0; i < BANDS_512; ++i) {
            float sel = 0.0f;
            for (size_t p = 0; p < lambda; ++p) {
                sel += (wacc[p] / wsum) * population[p][i];
            }
            // Paso de recombinación hacia el promedio ponderado de la selección.
            // 0.8 (antes 0.5): la media avanza más por generación para converger
            // en el presupuesto de generaciones.
            next[i] = m_meanGenome[i] + 0.8f * (sel - m_meanGenome[i]);
        }
    }

    // Adaptación de paso por éxito: fitness[0] es el fitness del genoma
    // medio ACTUAL (slot elite). Si mejoró respecto a la generación
    // anterior, el paso crece para explorar más; si no, se encoge para
    // afinar. El paso adaptado se usa en la SIGUIENTE generación.
    {
        if (fitness[0] > m_prevMeanFitness) {
            m_stepSize = std::fmin(kStepMax, m_stepSize * kStepUp);
        } else {
            m_stepSize = std::fmax(kStepMin, m_stepSize * kStepDown);
        }
        m_prevMeanFitness = fitness[0];
        if (!std::isfinite(m_stepSize)) m_stepSize = 0.5f; // guard numérico
    }

    for (size_t i = 0; i < BANDS_512; ++i) m_meanGenome[i] = next[i];
}

void EvolutionaryEQ::calibrate(float /*sampleRateHz*/) {
    // DETERMINISTA e idempotente: se parte del genoma neutral y se resiembra
    // el RNG, así que dos calibraciones producen el MISMO FIR (Ruta A ==
    // Ruta B, reproducible). Antes el RNG thread_local avanzaba entre
    // instancias y cada calibración daba un FIR distinto (defecto #1).
    // El objetivo está en frecuencia normalizada (referencia 48 kHz), así
    // que sampleRateHz no cambia el resultado (se ignora, como antes).
    for (size_t i = 0; i < BANDS_512; ++i) {
        m_meanGenome[i] = 0.0f;
    }
    m_stepSize = 0.5f;
    m_prevMeanFitness = -1e30f;
    m_rng.seed(1337);

    for (int g = 0; g < kCalibrationGenerations; ++g) {
        updateLM_CMA_ES();
    }
    rebuildFilterFromGenome();

    // Auto-verificación: el FIR aplicado debe ser finito y de pico <= 1.5
    // (el tope de rebuildFilterFromGenome; el FIR Harman natural tiene pico
    // ~1.05, así que 1.0 era demasiado estricto y hundía el nivel).
    bool finite = true;
    float peak = 0.0f;
    for (size_t t = 0; t < FIR_TAPS; ++t) {
        if (!std::isfinite(m_firCoeffsL[t])) finite = false;
        const float a = std::fabs(m_firCoeffsL[t]);
        if (a > peak) peak = a;
    }
    m_ready = finite && (peak <= 1.5f + 1e-4f);
}

void EvolutionaryEQ::resetHistory() noexcept {
    for (size_t i = 0; i < BLOCK_SIZE + FIR_TAPS; ++i) {
        m_histL[i] = 0.0f;
        m_histR[i] = 0.0f;
    }
}

void EvolutionaryEQ::processNEON(Ivanna::AudioBuffer* buffer) {
    processPlanar(buffer->left, buffer->right, BLOCK_SIZE);
}

// Procesa n frames planares con CUALQUIER n (la Ruta A entrega bloques de longitud variable). Se parte en
// trozos de <= BLOCK_SIZE sobre el mismo historial: con n == BLOCK_SIZE es bit-exacto con el codigo anterior
// de processNEON y partir un bloque en trozos no cambia la salida.
void EvolutionaryEQ::processPlanar(float* left, float* right, size_t n) {
    // GATE: sin calibración verificada el módulo es identidad bit-exacta.
    // Misma doctrina que SaFStimulusRenderer — nunca se activa un filtro sin
    // verificación real. Coste cero cuando no está habilitado.
    if (!m_ready || !left || !right) return;

    size_t done = 0;
    while (done < n) {
        const size_t c = std::min(BLOCK_SIZE, n - done);
        float* L = left + done;
        float* R = right + done;

        for (size_t i = 0; i < c; ++i) {
            m_histL[FIR_TAPS - 1 + i] = L[i];
            m_histR[FIR_TAPS - 1 + i] = R[i];
        }

        bool invalid = false;
        for (size_t i = 0; i < c; ++i) {
            float l_out = 0.0f;
            float r_out = 0.0f;

            for (size_t t = 0; t < FIR_TAPS; ++t) {
                l_out += m_firCoeffsL[t] * m_histL[i + t];
                r_out += m_firCoeffsR[t] * m_histR[i + t];
            }

            // Evitar doble saturación no-lineal: IvannaFusionEngine::process() ya
            // aplica fast_tanh al final de la cadena. Aquí solo se garantiza el
            // invariante de rango seguro [-1, 1] y finitud tras el FIR lineal.
            float l_final = std::fmin(1.0f, std::fmax(-1.0f, l_out));
            float r_final = std::fmin(1.0f, std::fmax(-1.0f, r_out));
            if (!std::isfinite(l_final) || !std::isfinite(r_final)) invalid = true;

            L[i] = l_final;
            R[i] = r_final;
        }

        // GUARD: si el FIR produjo una salida no finita (estado corrupto), se
        // deshabilita la aplicación y se vuelve a identidad verificada.
        if (invalid) {
            for (size_t i = 0; i < FIR_TAPS; ++i) {
                m_firCoeffsL[i] = (i == FIR_TAPS / 2) ? 1.0f : 0.0f;
                m_firCoeffsR[i] = (i == FIR_TAPS / 2) ? 1.0f : 0.0f;
            }
            m_ready = false;
            m_normalization = 1.0f;
        }

        for (size_t i = 0; i < FIR_TAPS - 1; ++i) {
            m_histL[i] = m_histL[c + i];
            m_histR[i] = m_histR[c + i];
        }
        done += c;
        if (!m_ready) return;   // el resto del bloque queda tal cual (identidad)
    }
}

} // namespace Ivanna
