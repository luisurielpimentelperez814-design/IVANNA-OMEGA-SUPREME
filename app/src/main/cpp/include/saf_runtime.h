#pragma once

#include <cmath>
#include <atomic>

struct SAFState {

    // Runtime SAF atomic controls
    std::atomic<float> gain{1.0f};
    std::atomic<float> compressor{0.0f};
    std::atomic<float> exciter{0.0f};
    std::atomic<float> spatial{0.0f};

    // Phi_SAF_infinity metric state
    double deltaE = 0.0;
    double metricNorm = 0.0;
    double memory = 0.0;

    // Metric parameters
    double Gt = 1.0;
    double epsilon = 1e-8;
    double lambda = 0.0;
};


// SAF_GUARD_CONVERGENCE (2026-09-17): paso limitado para no diverger.
// Antes el paso podía ser 100% del delta en un solo tick → oscilación
// audible. Se acotaa al 25% del delta por tick y se añade guard NaN,
// con la normalización por memoria que ya existía en el episolon.
inline double SAFUpdate(
        SAFState& state,
        double currentGain,
        double targetGain)
{
    double delta = targetGain - currentGain;
    if (!std::isfinite(delta)) return currentGain; // NaN guard: no propagar
    double safe_delta = delta;
    double max_step = 0.25 * safe_delta; // acotación del paso al 25% por tick

    state.deltaE = delta * delta;
    state.metricNorm = delta * delta;

    double eps = state.epsilon;

    double denominator =
        state.deltaE +
        state.metricNorm +
        state.lambda * state.memory +
        eps;

    double step =
        state.deltaE /
        denominator;

    state.memory =
        0.9 * state.memory +
        0.1 * std::abs(delta);

    double clipped = (step > 1.0) ? 1.0 : (step < 0.0 ? 0.0 : step);
    double applied = clipped * max_step;
    return currentGain + (applied < safe_delta ? applied : (applied > safe_delta ? applied : applied));
}


extern SAFState g_saf_state;
