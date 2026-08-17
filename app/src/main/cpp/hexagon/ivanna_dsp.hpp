#pragma once
/** ivanna_dsp.hpp — Hexagon DSP IDL types stub */
#include <cstdint>
struct IvannaDspParams {
    float    gain;
    float    eq_bands[10];
    uint32_t flags;
};
