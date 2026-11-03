#include "spatial/RouteAWfsEngine.hpp"
#include "spatial/WfsRenderer.hpp"
#include <cmath>
#include <cstdio>
#include <limits>

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
        return __LINE__; \
    } \
} while (false)

int main() {
    {
        ivanna::spatial::RouteAWfsEngine engine;
        CHECK(engine.prepare(48000.0f, 512));
        float left[512], right[512], dryL[512], dryR[512];
        for (int i = 0; i < 512; ++i) {
            left[i] = dryL[i] = 0.2f * std::sin(0.031f * static_cast<float>(i));
            right[i] = dryR[i] = 0.17f * std::sin(0.037f * static_cast<float>(i));
        }
        engine.setEnabled(false);
        // Each call represents a fresh upstream PCM block. The engine works
        // in-place, so restore the source before every callback rather than
        // accidentally feeding the previous callback's output back as input.
        for (int block = 0; block < 3; ++block) {
            for (int i = 0; i < 512; ++i) {
                left[i] = dryL[i];
                right[i] = dryR[i];
            }
            engine.process(left, right, 512);
        }
        // Three 512-frame blocks exceed the 15 ms bypass ramp at 48 kHz.
        for (int i = 0; i < 512; ++i) {
            CHECK(left[i] == dryL[i]);
            CHECK(right[i] == dryR[i]);
        }
        engine.setEnabled(true);
        bool changed = false;
        for (int block = 0; block < 8; ++block) {
            for (int i = 0; i < 512; ++i) {
                left[i] = dryL[i];
                right[i] = dryR[i];
            }
            engine.process(left, right, 512);
            for (int i = 0; i < 512; ++i) {
                CHECK(std::isfinite(left[i]));
                CHECK(std::isfinite(right[i]));
                changed = changed || std::fabs(left[i] - dryL[i]) > 1.0e-5f ||
                                     std::fabs(right[i] - dryR[i]) > 1.0e-5f;
            }
        }
        CHECK(changed);
    }

    // A long bypass must keep the renderer's delay history current. After
    // silence has replaced the old programme material, re-enabling WFS must
    // not leak samples from before the bypass.
    {
        ivanna::spatial::RouteAWfsEngine engine;
        CHECK(engine.prepare(48000.0f, 512));
        float left[512], right[512];
        engine.setEnabled(true);
        for (int block = 0; block < 6; ++block) {
            for (int i = 0; i < 512; ++i) {
                const float phase = static_cast<float>(block * 512 + i);
                left[i] = 0.45f * std::sin(0.021f * phase);
                right[i] = 0.38f * std::sin(0.027f * phase);
            }
            engine.process(left, right, 512);
        }

        engine.setEnabled(false);
        for (int block = 0; block < 32; ++block) {
            for (int i = 0; i < 512; ++i) left[i] = right[i] = 0.0f;
            engine.process(left, right, 512);
        }

        engine.setEnabled(true);
        for (int block = 0; block < 12; ++block) {
            for (int i = 0; i < 512; ++i) left[i] = right[i] = 0.0f;
            engine.process(left, right, 512);
            for (int i = 0; i < 512; ++i) {
                CHECK(std::isfinite(left[i]));
                CHECK(std::isfinite(right[i]));
                CHECK(std::fabs(left[i]) < 1.0e-5f);
                CHECK(std::fabs(right[i]) < 1.0e-5f);
            }
        }
    }

    {
        ivanna::spatial::RouteAWfsEngine engine;
        CHECK(engine.prepare(44100.0f, 256));
        const float x[7] = {0.35f, 3.15f, 0.25f, 3.25f, 0.25f, 3.25f, 3.0f};
        const float y[7] = {1.5f, 1.5f, 1.5f, 1.5f, 3.0f, 3.0f, 0.15f};
        const float z[7] = {0.0f, 0.0f, 3.5f, 3.5f, 3.5f, 3.5f, 3.0f};
        engine.setSpeakerLayout(x, y, z);
        engine.setSpread(2.0f);
        engine.setEnabled(true);
        float left[256]{}, right[256]{};
        for (int i = 0; i < 256; ++i)
            left[i] = right[i] = 0.1f * std::sin(0.02f * static_cast<float>(i));
        engine.process(left, right, 256);
        for (int i = 0; i < 256; ++i) {
            CHECK(std::isfinite(left[i]));
            CHECK(std::isfinite(right[i]));
        }
    }
    // WFS alias filtering is intentionally low-pass. The complementary
    // dry high-pass must retain programme treble when the WFS path is active.
    {
        ivanna::spatial::RouteAWfsEngine engine;
        CHECK(engine.prepare(48000.0f, 512));
        engine.setEnabled(true);
        float left[512], right[512];
        double energy = 0.0;
        for (int block = 0; block < 6; ++block) {
            for (int i = 0; i < 512; ++i) {
                const float phase = 6.2831853071795864769f *
                    8000.0f * static_cast<float>(block * 512 + i) / 48000.0f;
                left[i] = right[i] = 0.2f * std::sin(phase);
            }
            engine.process(left, right, 512);
            if (block == 5) {
                for (int i = 0; i < 512; ++i) {
                    CHECK(std::isfinite(left[i]));
                    CHECK(std::isfinite(right[i]));
                    energy += static_cast<double>(left[i]) * left[i];
                }
            }
        }
        const double rms = std::sqrt(energy / 512.0);
        CHECK(rms > 0.10); // full-band dry treble must not be discarded
    }

    // The bypass endpoint must remain bit-transparent even if the wet
    // renderer has produced a non-finite sample: IEEE-754 NaN * 0 is NaN.
    {
        ivanna::spatial::WfsRenderer renderer;
        constexpr int kFadeFrames = 720; // 15 ms at 48 kHz
        CHECK(renderer.init(48000.0f, kFadeFrames, 7));
        float dryL[kFadeFrames], dryR[kFadeFrames];
        float wetL[kFadeFrames], wetR[kFadeFrames];
        float outL[kFadeFrames]{}, outR[kFadeFrames]{};
        for (int i = 0; i < kFadeFrames; ++i) {
            dryL[i] = 0.23f;
            dryR[i] = -0.17f;
            wetL[i] = wetR[i] = std::numeric_limits<float>::quiet_NaN();
        }
        renderer.setEnabled(false);
        renderer.blendWithBypass(dryL, dryR, wetL, wetR,
                                 outL, outR, kFadeFrames);
        CHECK(outL[kFadeFrames - 1] == dryL[kFadeFrames - 1]);
        CHECK(outR[kFadeFrames - 1] == dryR[kFadeFrames - 1]);
        // Once settled, every sample must be copied from dry without
        // evaluating the potentially contaminated wet buffer.
        renderer.blendWithBypass(dryL, dryR, wetL, wetR,
                                 outL, outR, kFadeFrames);
        for (int i = 0; i < kFadeFrames; ++i) {
            CHECK(outL[i] == dryL[i]);
            CHECK(outR[i] == dryR[i]);
        }
    }
    return 0;
}
