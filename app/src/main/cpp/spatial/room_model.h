#pragma once
#include <cstdint>

void apply_reverb(const int16_t* __restrict__ in, int16_t* __restrict__ out, int samples, int delay_ms, float decay);
void apply_reverb_f(const float* __restrict__ in, float* __restrict__ out, int samples, int delay_ms, float decay);
