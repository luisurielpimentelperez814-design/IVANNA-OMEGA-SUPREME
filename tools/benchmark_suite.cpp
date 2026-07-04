#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <string>
#include <vector>

#include "../app/src/main/cpp/include/Compressor.h"
#include "../app/src/main/cpp/include/GainStage.h"
#include "../app/src/main/cpp/include/HarmonicExciter.h"
#include "../app/src/main/cpp/include/ParametricEQ.h"
#include "../app/src/main/cpp/include/StereoWidener.h"
#include "../app/src/main/cpp/visualizer/gammatone_filterbank13.hpp"

namespace {

struct Stats {
    double avg_ms = 0.0;
    double p95_ms = 0.0;
    double p99_ms = 0.0;
    double max_ms = 0.0;
    double cpu_percent = 0.0;
    double end_to_end_ms = 0.0;
    double jitter_ms = 0.0;
    double battery_mah_per_hour = 0.0;
};

double percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const double idx = std::clamp(p, 0.0, 1.0) * static_cast<double>(v.size() - 1);
    const auto lo = static_cast<size_t>(std::floor(idx));
    const auto hi = static_cast<size_t>(std::ceil(idx));
    const double frac = idx - static_cast<double>(lo);
    return v[lo] * (1.0 - frac) + v[hi] * frac;
}

Stats runBenchmark(int sampleRate, int blockFrames, int seconds) {
    const int blocks = std::max(1, seconds * sampleRate / blockFrames);
    std::vector<float> left(blockFrames), right(blockFrames), mono(blockFrames);
    std::vector<double> blockTimes;
    blockTimes.reserve(blocks);

    ivanna::DSPParams p{};
    p.sampleRate = static_cast<uint32_t>(sampleRate);
    p.drive = 0.72f;
    p.wet = 0.55f;
    p.mix = 0.80f;
    p.freq = 1000.0f;
    p.resonance = 0.85f;
    p.low = 2.0f;
    p.mid = 1.5f;
    p.high = 1.0f;
    p.presence = 0.5f;
    p.master = -1.0f;

    ivanna::ParametricEQ eq; eq.setParams(p);
    ivanna::Compressor comp; comp.setParams(p); comp.setThreshold(-18.0f); comp.setRatio(3.0f); comp.setAttack(5.0f); comp.setRelease(80.0f);
    ivanna::HarmonicExciter exciter; exciter.setParams(p); exciter.setAmount(0.45f);
    ivanna::StereoWidener widener; widener.setParams(p); widener.setWidth(1.25f);
    ivanna::GainStage gain; gain.setParams(p);
    ivanna::vis::GammatoneFilterBank13 fb; fb.init(static_cast<float>(sampleRate));
    float bands[ivanna::vis::GT_BANDS]{};

    double phaseL = 0.0, phaseR = 0.0;
    constexpr double twoPi = 6.28318530717958647692;
    constexpr double fL = 440.0;
    constexpr double fR = 554.37;

    for (int b = 0; b < blocks; ++b) {
        for (int i = 0; i < blockFrames; ++i) {
            left[i] = 0.55f * std::sin(phaseL) + 0.20f * std::sin(phaseL * 3.0);
            right[i] = 0.55f * std::sin(phaseR) + 0.20f * std::sin(phaseR * 2.5);
            mono[i] = 0.5f * (left[i] + right[i]);
            phaseL += twoPi * fL / sampleRate;
            phaseR += twoPi * fR / sampleRate;
            if (phaseL > twoPi) phaseL -= twoPi;
            if (phaseR > twoPi) phaseR -= twoPi;
        }

        auto t0 = std::chrono::high_resolution_clock::now();
        gain.processInput(left.data(), right.data(), blockFrames);
        eq.process(left.data(), right.data(), blockFrames);
        comp.process(left.data(), right.data(), blockFrames);
        exciter.process(left.data(), right.data(), blockFrames);
        widener.process(left.data(), right.data(), blockFrames);
        gain.processOutput(left.data(), right.data(), blockFrames);
        fb.process(mono.data(), blockFrames, bands);
        auto t1 = std::chrono::high_resolution_clock::now();

        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        blockTimes.push_back(ms);
    }

    Stats st;
    st.avg_ms = std::accumulate(blockTimes.begin(), blockTimes.end(), 0.0) / blockTimes.size();
    st.p95_ms = percentile(blockTimes, 0.95);
    st.p99_ms = percentile(blockTimes, 0.99);
    st.max_ms = *std::max_element(blockTimes.begin(), blockTimes.end());

    const double buffer_budget_ms = 1000.0 * blockFrames / sampleRate;
    st.cpu_percent = (st.avg_ms / buffer_budget_ms) * 100.0;
    st.end_to_end_ms = buffer_budget_ms + st.avg_ms;
    st.jitter_ms = std::max(0.0, st.p99_ms - st.avg_ms);

    // Moto G85 official battery reference: 5000 mAh / 34 h ≈ 147.06 mA average device draw.
    // Coarse DSP estimate: average current * real-time CPU duty.
    constexpr double motoG85AvgCurrentMa = 5000.0 / 34.0;
    st.battery_mah_per_hour = motoG85AvgCurrentMa * (st.cpu_percent / 100.0);
    return st;
}

} // namespace

int main(int argc, char** argv) {
    int sampleRate = 48000;
    int blockFrames = 256;
    int seconds = 15;
    if (argc > 1) sampleRate = std::stoi(argv[1]);
    if (argc > 2) blockFrames = std::stoi(argv[2]);
    if (argc > 3) seconds = std::stoi(argv[3]);

    const Stats st = runBenchmark(sampleRate, blockFrames, seconds);
    std::printf("IVANNA benchmark suite\n");
    std::printf("sample_rate_hz=%d\nblock_frames=%d\nduration_s=%d\n", sampleRate, blockFrames, seconds);
    std::printf("avg_block_ms=%.6f\np95_block_ms=%.6f\np99_block_ms=%.6f\nmax_block_ms=%.6f\n", st.avg_ms, st.p95_ms, st.p99_ms, st.max_ms);
    std::printf("realtime_cpu_percent=%.4f\nend_to_end_latency_ms=%.6f\njitter_ms=%.6f\n", st.cpu_percent, st.end_to_end_ms, st.jitter_ms);
    std::printf("estimated_battery_mah_per_hour=%.6f\n", st.battery_mah_per_hour);
    return 0;
}
