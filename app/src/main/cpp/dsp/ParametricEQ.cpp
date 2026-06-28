#include "../include/ParametricEQ.h"
#include <cstring>

namespace ivanna {

void ParametricEQ::setParams(const DSPParams& p) {
    if (p.sampleRate < 8000 || p.sampleRate > 768000) {
        return;
    }

    double sr = static_cast<double>(p.sampleRate);
    lowL.setLowShelf (80.0, 0.707, p.low, sr);  lowR = lowL;
    midL.setPeaking  (p.freq, p.resonance, p.mid, sr);  midR = midL;
    highL.setHighShelf(8000.0, 0.707, p.high, sr); highR = highL;
    presL.setPeaking (4000.0, 1.2, p.presence, sr); presR = presL;
    last_ = p;
}

__attribute__((hot, flatten))
void ParametricEQ::process(float* __restrict__ left, float* __restrict__ right, int frames) {
    if (frames <= 0) return;
    if (!left || !right) return;

    #pragma clang loop vectorize(enable) interleave(enable)
    for (int i = 0; i < frames; ++i) {
        float l = left[i];
        float r = right[i];

        l = lowL.process(l);
        l = midL.process(l);
        l = highL.process(l);
        l = presL.process(l);

        r = lowR.process(r);
        r = midR.process(r);
        r = highR.process(r);
        r = presR.process(r);

        left[i] = l;
        right[i] = r;
    }
}

void ParametricEQ::reset() {
    lowL.reset(); lowR.reset();
    midL.reset(); midR.reset();
    highL.reset(); highR.reset();
    presL.reset(); presR.reset();
}

} // namespace ivanna
