#pragma once

#include <cmath>
#include <algorithm>
#include <atomic>

// SAFState defined in saf_runtime.h
#include "saf_runtime.h"



inline double SAFProjection(double x)
{
    return std::max(0.0, std::min(2.0, x));
}


inline double SAFMathUpdate(
        SAFState& s,
        double p,
        double target)
{
    double delta = target - p;

    double dEnergy = std::abs(delta);

    double normGt =
        s.Gt * delta * delta;


    s.memory =
        0.9 * s.memory +
        0.1 * dEnergy;


    double alpha =
        dEnergy /
        (
            dEnergy +
            normGt +
            s.lambda * s.memory +
            s.epsilon
        );


    double correction =
        alpha *
        (1.0 / s.Gt) *
        delta;


    double phi =
        SAFProjection(
            p + correction
        );


    s.deltaE = dEnergy;
    s.metricNorm = normGt;


    s.gain.store(
        static_cast<float>(phi),
        std::memory_order_relaxed
    );


    return phi;
}
