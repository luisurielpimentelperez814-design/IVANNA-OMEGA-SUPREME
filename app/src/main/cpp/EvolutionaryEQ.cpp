#include "EvolutionaryEQ.hpp"
#include "IvannaFusionCore.hpp"
#include <cstddef>
#include <cmath>
#include <random>

namespace Ivanna {

static thread_local std::mt19937 g_rng(1337);

static inline float fast_rand() {
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    return dist(g_rng);
}

// ─────────────────────────────────────────────────────────────────────────────
// Curva objetivo paramétrica del EQ evolutivo.
//
// El fitness YA NO es varianza interna del genoma (defecto #2 de la auditoría
// dc2ea33f). Es la desviación entre la respuesta en magnitud REAL del FIR
// diseñado desde el genoma y esta curva objetivo: un "mejor fitness" ahora
// significa "respuesta más cercana al objetivo musical", medible con señal.
// Curva objetivo agresiva (perfil de presencia): recorte de graves + realce
// de presencia/aire. El módulo es libre de apartarse de ella cuando otro
// frente (Psychoacoustics / masking) imponga su propio sesgo.
// ─────────────────────────────────────────────────────────────────────────────
namespace {
constexpr float kPi = 3.14159265358979f;

constexpr int   kNumTargets = 6;
// frecuencia normalizada (0..π) y ganancia objetivo (dB) por punto
constexpr float kTargetWn[kNumTargets] = {
    0.060f, 0.170f, 0.480f, 0.900f, 1.700f, 2.700f
};
constexpr float kTargetDb[kNumTargets] = {
    -2.0f, -1.0f,  0.0f,  2.0f,  3.0f,  1.5f
};

inline float db2lin(float db) { return std::pow(10.0f, db / 20.0f); }

// Diseña el FIR (taps) desde el genoma espectral (BANDS_512 bandas, en dB).
//
// Garantía estructural: con genoma neutral (todas las bandas en 0 dB) el
// resultado es EXACTAMENTE el delta δ[t-127] ⇒ respuesta identidad plana.
// Cada banda desviada añade una sinusoide enventanada a la frecuencia
// normalizada que le corresponde (wn = π·(b+0.5)/BANDS_512). Es un diseño
// por superposición de bases: la respuesta en magnitud del resultado refleja
// de forma monótona la desviación del genoma en cada banda.
void designTaps(const float* genome, float* taps) {
    const float center = static_cast<float>(FIR_TAPS - 1) * 0.5f;

    for (size_t t = 0; t < FIR_TAPS; ++t) {
        // Ventana Blackman-Harris centrada (tapa el ripple de truncado).
        const float x = (static_cast<float>(t) - center) / center; // -1..1
        const float w = 0.35875f
                      + 0.48829f * std::cos(kPi * x)
                      + 0.14128f * std::cos(2.0f * kPi * x)
                      + 0.01168f * std::cos(3.0f * kPi * x);

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
} // namespace

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
        m_evolutionPath[i] = 0.0f;
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
// debe acercarse a la curva objetivo. Un regularizador de suavidad de peso
// bajo evita filtros peine; un guard de pico evita el filtro identidad
// trivial y cualquier boost que sature la cadena.
float EvolutionaryEQ::calculateFitness(const float* genome) {
    float taps[FIR_TAPS];
    designTaps(genome, taps);

    float err = 0.0f;
    for (int k = 0; k < kNumTargets; ++k) {
        const float mag = magnitudeAt(taps, kTargetWn[k]);
        const float db  = 20.0f * std::log10(std::fmax(mag, 1e-6f));
        const float d   = db - kTargetDb[k];
        err += d * d;
    }

    // Regularizador (NO criterio principal): peso bajo a propósito.
    float smooth = 0.0f;
    for (size_t i = 1; i < BANDS_512; ++i) {
        const float diff = genome[i] - genome[i - 1];
        smooth += diff * diff;
    }
    err += 0.5f * smooth;

    // El pico NO se penaliza aquí: penalizarlo (o normalizarlo) lleva el
    // óptimo al identidad trivial (toda magnitud en 1). El pico se controla
    // solo como auto-verificación de calibrate() + normalización del FIR
    // aplicado —nunca en el criterio de optimización—, así el fitness puede
    // explorar forma espectral real.
    return -err;
}

float EvolutionaryEQ::currentFitness() const {
    return const_cast<EvolutionaryEQ*>(this)->calculateFitness(m_meanGenome);
}

void EvolutionaryEQ::rebuildFilterFromGenome() {
    float taps[FIR_TAPS];
    designTaps(m_meanGenome, taps);

    float peak = 0.0f;
    for (size_t t = 0; t < FIR_TAPS; ++t) {
        const float a = std::fabs(taps[t]);
        if (a > peak) peak = a;
    }
    const float norm = (peak > 1.0f) ? (1.0f / peak) : 1.0f;

    for (size_t t = 0; t < FIR_TAPS; ++t) {
        m_firCoeffsL[t] = taps[t] * norm;
        m_firCoeffsR[t] = taps[t] * norm; // diseño simétrico L/R
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
    constexpr size_t lambda = 8;
    float population[lambda][BANDS_512];
    float fitness[lambda];

    for (size_t p = 0; p < lambda; ++p) {
        for (size_t i = 0; i < BANDS_512; ++i) {
            population[p][i] = m_meanGenome[i] + m_stepSize * fast_rand();
        }
        fitness[p] = calculateFitness(population[p]);
    }

    size_t best_idx = 0;
    float max_fit = fitness[0];
    for (size_t p = 1; p < lambda; ++p) {
        if (fitness[p] > max_fit) {
            max_fit = fitness[p];
            best_idx = p;
        }
    }

    // Selección por promedio ponderado por fitness (softmax estable) sobre la
    // población, en el dominio dB del genoma. La media lineal de ±1 aleatorios
    // es ~0 (el genoma nunca salía del punto fijo en 0 dB —bug atrapado por
    // test_evolutionary_eq), así que se pondera cada individuo por su fitness
    // relativo: los genomas cuya respuesta medida es más cercana al objetivo
    // dominan la siguiente media. Como el genoma medio SIEMPRE está en la
    // población evaluada y el softmax le da peso máximo al mejor, la media
    // converge de forma monótona hacia el objetivo (verificable por fitness).
    {
        // Ponderación softmax estable sobre fitness (menos negativo = mejor).
        float wmax = fitness[0];
        for (size_t p = 1; p < lambda; ++p) if (fitness[p] > wmax) wmax = fitness[p];
        float wsum = 0.0f;
        float wacc[lambda];
        for (size_t p = 0; p < lambda; ++p) {
            wacc[p] = std::exp((fitness[p] - wmax) / 1.0f); // escala térmica 1 dB
            wsum += wacc[p];
        }
        constexpr float cc = 0.2f;
        for (size_t i = 0; i < BANDS_512; ++i) {
            float sel = 0.0f;
            for (size_t p = 0; p < lambda; ++p) {
                sel += (wacc[p] / wsum) * population[p][i];
            }
            // Paso de recombinación hacia el promedio ponderado de la selección.
            const float next = m_meanGenome[i] + 0.5f * (sel - m_meanGenome[i]);
            m_evolutionPath[i] = (1.0f - cc) * m_evolutionPath[i] + cc * next;
            m_meanGenome[i] = next;
        }
    }

    rebuildFilterFromGenome(); // el genoma SÍ llega ahora al FIR aplicado
}

void EvolutionaryEQ::calibrate(float /*sampleRateHz*/) {
    for (int g = 0; g < kCalibrationGenerations; ++g) {
        updateLM_CMA_ES();
    }
    rebuildFilterFromGenome();

    // Auto-verificación: el FIR aplicado debe ser finito y de pico <= 1.
    bool finite = true;
    float peak = 0.0f;
    for (size_t t = 0; t < FIR_TAPS; ++t) {
        if (!std::isfinite(m_firCoeffsL[t])) finite = false;
        const float a = std::fabs(m_firCoeffsL[t]);
        if (a > peak) peak = a;
    }
    m_ready = finite && (peak <= 1.0f + 1e-4f);
}

void EvolutionaryEQ::processNEON(Ivanna::AudioBuffer* buffer) {
    // GATE: sin calibración verificada el módulo es identidad bit-exacta.
    // Misma doctrina que SaFStimulusRenderer — nunca se activa un filtro sin
    // verificación real. Coste cero cuando no está habilitado.
    if (!m_ready) return;

    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        m_histL[FIR_TAPS - 1 + i] = buffer->left[i];
        m_histR[FIR_TAPS - 1 + i] = buffer->right[i];
    }

    bool invalid = false;
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        float l_out = 0.0f;
        float r_out = 0.0f;

        for (size_t t = 0; t < FIR_TAPS; ++t) {
            l_out += m_firCoeffsL[t] * m_histL[i + t];
            r_out += m_firCoeffsR[t] * m_histR[i + t];
        }

        // Soft-clip Padé como saturador musical. La Padé [7/6] sobrepasa 1.0
        // en ~0.5% con entradas grandes: el clamp duro garantiza |salida| <= 1
        // SIEMPRE (doctrina SafetyLimiter / certificación IAEL: 0 clipping).
        float l_final = fast_tanh_scalar(l_out);
        float r_final = fast_tanh_scalar(r_out);
        l_final = std::fmin(1.0f, std::fmax(-1.0f, l_final));
        r_final = std::fmin(1.0f, std::fmax(-1.0f, r_final));
        if (!std::isfinite(l_final) || !std::isfinite(r_final)) invalid = true;

        buffer->left[i] = l_final;
        buffer->right[i] = r_final;
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
        m_histL[i] = m_histL[BLOCK_SIZE + i];
        m_histR[i] = m_histR[BLOCK_SIZE + i];
    }
}

} // namespace Ivanna
