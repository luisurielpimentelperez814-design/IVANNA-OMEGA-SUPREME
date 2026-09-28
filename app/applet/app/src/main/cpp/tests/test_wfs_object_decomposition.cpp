// test_wfs_object_decomposition.cpp — EJE 1: descomposición estéreo→objetos
// alimentando WfsRenderer real. Verifica energía por objeto, posiciones
// canónicas, ausencia de NaN y que el renderer los consume a salida finita.
#include "../spatial/WfsRenderer.hpp"
#include "../spatial/StereoObjectDecomposer.hpp"
#include <cmath>
#include <cstdio>
#include <vector>
#include <algorithm>

using ivanna::spatial::WfsRenderer;
using ivanna::spatial::StereoObjectDecomposer;
static int g_fail = 0;
#define CHECK(c,m) do{ if(!(c)){std::printf("FALLARON: %s\n",m);++g_fail;} else std::printf("  [ok] %s\n",m);}while(0)
static float maxAbs(const float* v,int n){float m=0;for(int i=0;i<n;i++){float a=std::fabs(v[i]); if(a>m)m=a;} return m;}

int main(){
    constexpr int F=384; constexpr float sr=48000.f;
    StereoObjectDecomposer dec;
    CHECK(dec.prepare(sr,F),"decomposer prepare()");
    WfsRenderer wfs; CHECK(wfs.init(sr,F,16),"renderer init()");

    // Entrada: voz al centro (mono) + guitarra solo a la izquierda.
    std::vector<float> buf(F*2);
    for(int i=0;i<F;i++){
        float voz=0.6f*std::sin(2.f*3.14159265f*220.f*i/sr);
        float gtr=0.3f*std::sin(2.f*3.14159265f*880.f*i/sr);
        buf[2*i]=voz+gtr; buf[2*i+1]=voz;
    }
    dec.processBlock(buf.data(), F);

    CHECK(maxAbs(dec.objectL(0),F)>0.1f && maxAbs(dec.objectR(0),F)>0.1f,"CENTER con energia (voz)");
    CHECK(maxAbs(dec.objectL(1),F)>0.05f && maxAbs(dec.objectR(1),F)<1e-6f,"LEFT flank solo en L");
    CHECK(maxAbs(dec.objectR(2),F)>0.05f && maxAbs(dec.objectL(2),F)<1e-6f,"RIGHT flank solo en R");
    CHECK(maxAbs(dec.objectL(3),F)>0.01f,"AMBIENTE residual presente");
    for(int o=0;o<4;o++) CHECK(std::isfinite(maxAbs(dec.objectL(o),F)) && std::isfinite(maxAbs(dec.objectR(o),F)),"objeto finito");

    float x[4],y[4],g[4]; dec.canonicalLayout(x,y,g);
    CHECK(x[0]==0.f && y[0]>0.f,"CENTER al frente");
    CHECK(x[1]<0.f && x[2]>0.f,"flanks L/R a lados opuestos");
    CHECK(y[3]>y[0],"AMBIENTE mas atras que el centro");

    // El renderer consume los 4 objetos (buffers frescos por bloque, como el pipeline real).
    std::vector<float> L(F,0.f),R(F,0.f);
    const float* in[4]={dec.objectL(0),dec.objectL(1),dec.objectL(2),dec.objectL(3)};
    for(int o=0;o<4;o++) wfs.setObject(o, x[o], y[o], g[o]);
    wfs.process(in,4,L.data(),R.data(),F);
    CHECK(std::isfinite(maxAbs(L.data(),F)) && std::isfinite(maxAbs(R.data(),F)),"salida renderer finita");
    CHECK(maxAbs(L.data(),F) < 8.0f && maxAbs(R.data(),F) < 8.0f,"salida renderer acotada (sin explosion)");

    if(g_fail==0) std::printf("PASSED: test_wfs_object_decomposition OK\n");
    return g_fail?1:0;
}
