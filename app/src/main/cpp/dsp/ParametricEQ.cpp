#include "../include/ParametricEQ.h"
#include <cstring>

namespace ivanna {

void ParametricEQ::setParams(const DSPParams& p) {
    double sr = p.sampleRate;
    lowL.setLowShelf (80.0,  0.707, p.low,      sr); lowR = lowL;
    midL.setPeaking   (p.freq, p.resonance, p.mid, sr); midR = midL;
    highL.setHighShelf(8000.0, 0.707, p.high,    sr); highR = highL;
    presL.setPeaking  (4000.0, 1.2,  p.presence, sr); presR = presL;
    last_ = p;
}

void ParametricEQ::process(float* left, float* right, int frames) {
    for (int i = 0; i < frames; ++i) {
        float l = left[i],  r = right[i];
        l = lowL.process(l);  r = lowR.process(r);
        l = midL.process(l);  r = midR.process(r);
        l = highL.process(l); r = highR.process(r);
        l = presL.process(l); r = presR.process(r);
        left[i] = l; right[i] = r;
    }
}

void ParametricEQ::reset() {
    lowL.reset(); lowR.reset();
    midL.reset(); midR.reset();
    highL.reset(); highR.reset();
    presL.reset(); presR.reset();
}

} // namespace ivanna
