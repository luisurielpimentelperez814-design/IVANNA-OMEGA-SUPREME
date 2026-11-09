// Auto-evolución 0001 (2026-11-09): SAF_GUARD para PhiSAFInfinity.
// Su función hermana SAFUpdate (saf_runtime.h, 2026-09-17) ya tiene
// guarda NaN + denominador degenerado; esta ruta no la tenía.
// Repro medido antes del fix (g++ host, /tmp/repro_saf.cpp):
//   una sola entrada no-finita (deltaE/delta/G/memory NaN o Inf)
//   o G<=0 dejaba el gain clavado en 2.0 (máximo) por el clamp final,
//   y la sanitización de G ocurría DESPUÉS de usarla en norm/alpha.
// Fail-closed: entrada no-finita, G degenerada (<=1e-6, seis órdenes
// bajo el nominal Gt=1.0) o denominador degenerado → se devuelve el
// gain actual sin tocar el estado (mismo "paso cero seguro" de SAFUpdate).
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
    const double current = state.gain.load();

    if (!std::isfinite(deltaE) || !std::isfinite(delta) ||
        !std::isfinite(G) || !std::isfinite(memory))
        return current; // NaN guard: no propagar, no tocar el estado

    // G degenerada (<=0 o ~0): con el piso 1e-6 el update sería
    // alpha*delta*1e6, numéricamente sin sentido (clava el gain en
    // un rail con cualquier delta no nulo). Paso cero seguro.
    if (!(G > 1e-6))
        return current;

    double norm =
        delta * G * delta;

    double denominator =
        deltaE + norm + 0.01 * memory + 1e-8;
    if (denominator <= 0.0 || !std::isfinite(denominator))
        return current; // denominador degenerado: paso cero seguro

    double alpha =
        deltaE / denominator;

    double update =
        alpha * (delta / G);

    if (!std::isfinite(update))
        return current; // update no finito: no tocar el estado

    double p =
        current + update;

    p = std::max(0.0, std::min(2.0, p));

    state.deltaE = deltaE;
    state.metricNorm = norm;
    state.memory = memory;
    state.gain.store(p);

    return p;
}
