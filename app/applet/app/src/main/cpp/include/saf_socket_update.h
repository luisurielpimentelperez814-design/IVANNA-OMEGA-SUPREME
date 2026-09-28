#pragma once

#include "saf_runtime.h"

inline void updateSAFFromJson(
    float gain,
    float compressor,
    float exciter,
    float spatial
)
{
    g_saf_state.gain.store(gain);
    g_saf_state.compressor.store(compressor);
    g_saf_state.exciter.store(exciter);
    g_saf_state.spatial.store(spatial);
}
