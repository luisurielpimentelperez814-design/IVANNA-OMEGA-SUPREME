#include "saf_runtime.h"
#include <cmath>
#include <algorithm>

double PhiSAFInfinity(
        SAFState& state,
        double deltaE,
        double delta,
        double G,
        double memory)
{
    double norm =
        delta * G * delta;

    double alpha =
        deltaE /
        (deltaE + norm + 0.01 * memory + 1e-8);

    double update =
        alpha * (delta / G);

    double p =
        state.gain.load() + update;

    p = std::max(0.0, std::min(2.0, p));

    state.deltaE = deltaE;
    state.metricNorm = norm;
    state.memory = memory;
    state.gain.store(p);

    return p;
}
