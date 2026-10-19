#pragma once

#include <cmath>

namespace ivanna::safmus {

struct EvolutionCandidateEvidence {
    float fitness;
    bool routeEnabled;
    bool signalFinite;
    bool candidateFinite;
    bool candidateBounded;
    bool headroomValid;
};

// The score threshold is the existing kernel's contract. The gate adds explicit
// numeric, route, bounded-gain, and headroom evidence before a genome reaches DSP.
inline bool mayApplyEvolutionCandidate(const EvolutionCandidateEvidence& e) noexcept {
    return e.routeEnabled &&
           e.signalFinite &&
           e.candidateFinite &&
           e.candidateBounded &&
           e.headroomValid &&
           std::isfinite(e.fitness) &&
           e.fitness > 0.5f;
}

} // namespace ivanna::safmus
