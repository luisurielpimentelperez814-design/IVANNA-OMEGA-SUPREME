#pragma once

#include <cmath>
#include <atomic>

struct SAFState {

    double gain = 1.0;

    double deltaE = 0.0;

    double metricNorm = 0.0;

    double memory = 0.0;

};


inline double SAFUpdate(
        SAFState& state,
        double currentGain,
        double targetGain)
{

    double delta =
        targetGain - currentGain;


    state.deltaE =
        delta * delta;


    state.metricNorm =
        delta * delta;


    double lambda = 0.05;
    double eps = 0.00001;


    double denominator =
        state.deltaE +
        state.metricNorm +
        lambda * state.memory +
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
