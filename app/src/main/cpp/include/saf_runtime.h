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


inline double SAFUpdate(
        SAFState& state,
        double currentGain,
        double targetGain)
{
    double delta = targetGain - currentGain;

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

    return currentGain + step * delta;
}


extern SAFState g_saf_state;
