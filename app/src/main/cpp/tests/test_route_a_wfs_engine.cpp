#include "spatial/RouteAWfsEngine.hpp"
#include <cassert>
#include <cmath>

int main() {
    {
        ivanna::spatial::RouteAWfsEngine engine;
        assert(engine.prepare(48000.0f, 512));
        float left[512], right[512], dryL[512], dryR[512];
        for (int i=0;i<512;++i) {
            left[i]=dryL[i]=0.2f*std::sin(0.031f*static_cast<float>(i));
            right[i]=dryR[i]=0.17f*std::sin(0.037f*static_cast<float>(i));
        }
        engine.setEnabled(false);
        for (int block=0; block<3; ++block) engine.process(left,right,512);
        for(int i=0;i<512;++i) {
            assert(left[i] == dryL[i]);
            assert(right[i] == dryR[i]);
        }
        engine.setEnabled(true);
        bool changed=false;
        for(int block=0;block<8;++block) {
            engine.process(left,right,512);
            for(int i=0;i<512;++i) {
                assert(std::isfinite(left[i]));
                assert(std::isfinite(right[i]));
                changed = changed || std::fabs(left[i]-dryL[i])>1.0e-5f ||
                                     std::fabs(right[i]-dryR[i])>1.0e-5f;
            }
        }
        assert(changed);
    }
    {
        ivanna::spatial::RouteAWfsEngine engine;
        assert(engine.prepare(44100.0f,256));
        const float x[7]={0.35f,3.15f,0.25f,3.25f,0.25f,3.25f,3.0f};
        const float y[7]={1.5f,1.5f,1.5f,1.5f,3.0f,3.0f,0.15f};
        const float z[7]={0.0f,0.0f,3.5f,3.5f,3.5f,3.5f,3.0f};
        engine.setSpeakerLayout(x,y,z);
        engine.setSpread(2.0f);
        engine.setEnabled(true);
        float l[256]{},r[256]{};
        for(int i=0;i<256;++i) l[i]=r[i]=0.1f*std::sin(0.02f*i);
        engine.process(l,r,256);
        for(int i=0;i<256;++i) {
            assert(std::isfinite(l[i]));
            assert(std::isfinite(r[i]));
        }
    }
    return 0;
}
