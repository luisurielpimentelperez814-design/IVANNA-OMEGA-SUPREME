// test_dsp_chain.cpp — IVANNA DSP chain validation tests
#include <cassert>
#include <cmath>
#include <cstdio>
#include "../../../include/dsp_types.h"
#include "../../../include/ParametricEQ.h"
#include "../../../include/Compressor.h"
#include "../../../include/SafetyLimiter.h"
#include "../../../dsp/ParametricEQ.cpp"
#include "../../../dsp/Compressor.cpp"
#include "../../../dsp/SafetyLimiter.cpp"

namespace ivanna {

void test_eq_no_nan() {
    ParametricEQ eq;
    float l[256] = {};
    float r[256] = {};
    for (int i = 0; i < 256; i++) {
        l[i] = sinf(i * 0.1f);
        r[i] = cosf(i * 0.1f);
    }
    DSPParams p{};
    p.low = 6.0f; p.mid = -3.0f; p.high = 6.0f; p.presence = 0.0f;
    eq.setParams(p);
    eq.process(l, r, 256);
    for (int i = 0; i < 256; i++) {
        assert(!std::isnan(l[i]) && !std::isinf(l[i]));
        assert(!std::isnan(r[i]) && !std::isinf(r[i]));
    }
    printf("[PASS] EQ: no NaN/Inf\n");
}

void test_limiter_clamps() {
    SafetyLimiter lim;
    float l[512];
    float r[512];
    for (int i = 0; i < 512; i++) { l[i] = 5.0f; r[i] = -5.0f; }
    lim.process(l, r, 512);
    for (int i = 0; i < 512; i++) {
        assert(std::fabs(l[i]) <= 1.0f + 1e-5f);
        assert(std::fabs(r[i]) <= 1.0f + 1e-5f);
    }
    assert(lim.getClipCount() > 0);
    printf("[PASS] Limiter: clamps to ±1.0\n");
}

void test_compressor_no_overflow() {
    Compressor comp;
    float l[512];
    float r[512];
    for (int i = 0; i < 512; i++) { l[i] = (i % 2 == 0) ? 0.9f : -0.9f; r[i] = -l[i]; }
    DSPParams p{};
    p.alpha = 0.5f; p.beta = 0.5f; p.gamma = 0.5f;
    comp.setParams(p);
    comp.process(l, r, 512);
    for (int i = 0; i < 512; i++) {
        assert(std::fabs(l[i]) <= 2.0f);
        assert(std::fabs(r[i]) <= 2.0f);
    }
    printf("[PASS] Compressor: no overflow\n");
}

} // namespace ivanna

int main() {
    ivanna::test_eq_no_nan();
    ivanna::test_limiter_clamps();
    ivanna::test_compressor_no_overflow();
    printf("ALL DSP TESTS PASSED\n");
    return 0;
}
