/**
 * test_block_peak_guard.cpp — la extracción de BlockPeakGuard no cambia el comportamiento.
 *
 * kRef es la copia LITERAL del bloque que vivía inline en nativeProcess antes de extraerlo.
 * La función compartida debe producir exactamente los mismos bits en cualquier secuencia.
 */
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "BlockPeakGuard.h"

namespace {
void refGuard(float* lBuf, float* rBuf, int n, float& state) {   // copia literal del original
    float pk = 0.f;
    for (int i = 0; i < n; ++i) {
        const float al = lBuf[i] < 0.f ? -lBuf[i] : lBuf[i];
        const float ar = rBuf[i] < 0.f ? -rBuf[i] : rBuf[i];
        if (al > pk) pk = al;
        if (ar > pk) pk = ar;
    }
    const float targetSc = (pk > 0.89f && pk > 1e-9f) ? (0.89f / pk) : 1.0f;
    const float startSc = state;
    if (targetSc < startSc) {
        for (int i = 0; i < n; ++i) { lBuf[i] *= targetSc; rBuf[i] *= targetSc; }
        state = targetSc;
    } else if (targetSc > startSc) {
        for (int i = 0; i < n; ++i) {
            const float t = (n > 1) ? (float)i / (float)(n - 1) : 1.0f;
            const float sc = startSc + (targetSc - startSc) * t;
            lBuf[i] *= sc;
            rBuf[i] *= sc;
        }
        state = targetSc;
    }
}
uint32_t lcg(uint32_t& s) { s = s * 1664525u + 1013904223u; return s; }
} // namespace

TEST(BlockPeakGuard, BitIdenticalToTheOriginalInlineLogic) {
    for (int n : {1, 64, 480, 512}) {
        uint32_t seed = 1234u + (uint32_t)n;
        float sa = 1.0f, sb = 1.0f;
        for (int blk = 0; blk < 3000; ++blk) {
            // Nivel que oscila entre muy bajo y muy por encima del techo, con ráfagas.
            const float amp = (blk % 7 == 0) ? 1.6f : 0.05f + 1.2f * ((lcg(seed) >> 9) / 8388608.0f);
            std::vector<float> a(n), b(n), c(n), d(n);
            for (int i = 0; i < n; ++i) {
                a[i] = c[i] = amp * (((lcg(seed) >> 8) / 8388608.0f) - 1.0f);
                b[i] = d[i] = amp * (((lcg(seed) >> 8) / 8388608.0f) - 1.0f);
            }
            refGuard(a.data(), b.data(), n, sa);
            ivanna::blockPeakGuard(c.data(), d.data(), n, sb);
            ASSERT_EQ(0, std::memcmp(a.data(), c.data(), n * sizeof(float))) << "L n=" << n << " blk=" << blk;
            ASSERT_EQ(0, std::memcmp(b.data(), d.data(), n * sizeof(float))) << "R n=" << n << " blk=" << blk;
            ASSERT_EQ(sa, sb);
        }
    }
}

TEST(BlockPeakGuard, QuietSignalPassesUntouched) {
    std::vector<float> l(480, 0.3f), r(480, -0.2f), l0(l), r0(r);
    float st = 1.0f;
    ivanna::blockPeakGuard(l.data(), r.data(), 480, st);
    EXPECT_EQ(l, l0); EXPECT_EQ(r, r0); EXPECT_EQ(st, 1.0f);   // bypass exacto bajo el techo
}

TEST(BlockPeakGuard, LoudBlockIsBroughtToTheCeilingImmediately) {
    std::vector<float> l(480, 0.0f), r(480, 0.0f);
    l[100] = 1.78f;                                            // pico 2x el techo
    float st = 1.0f;
    ivanna::blockPeakGuard(l.data(), r.data(), 480, st);
    float pk = 0.f; for (float v : l) pk = std::max(pk, std::fabs(v));
    EXPECT_NEAR(pk, 0.89f, 1e-6f);
    EXPECT_NEAR(st, 0.5f, 1e-6f);
}

TEST(BlockPeakGuard, ZeroLengthAndSilenceAreSafe) {
    float st = 1.0f, a = 0.f, b = 0.f;
    ivanna::blockPeakGuard(&a, &b, 0, st);
    EXPECT_EQ(st, 1.0f);
    std::vector<float> z(480, 0.f), z2(480, 0.f);
    ivanna::blockPeakGuard(z.data(), z2.data(), 480, st);
    EXPECT_EQ(st, 1.0f);
}
