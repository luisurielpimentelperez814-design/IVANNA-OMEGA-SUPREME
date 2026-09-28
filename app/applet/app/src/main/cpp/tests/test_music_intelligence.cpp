// test_music_intelligence.cpp — verificación del Music Intelligence Engine.
//   g++ -std=c++17 -Wall -Wextra -I../music_intelligence test_music_intelligence.cpp \
//       ../music_intelligence/MusicFeatureExtractor.cpp \
//       ../music_intelligence/MusicIntelligenceEngine.cpp -o test_ime && ./test_ime
#include "../music_intelligence/MusicFeatureExtractor.hpp"
#include "../music_intelligence/MusicIntelligenceEngine.hpp"
#include <cstdio>
#include <cmath>
#include <vector>

using namespace ivanna::ime;
static int g_fail=0;
#define EXPECT(c,m) do{ if(!(c)){std::printf("  [FAIL] %s\n",m);++g_fail;} else {std::printf("  [ok]   %s\n",m);} }while(0)

static void genSine(std::vector<float>& l, std::vector<float>& r, float sr, float f, float width){
    size_t n=l.size();
    for(size_t i=0;i<n;++i){ float s=0.5f*std::sin(2.f*3.14159265f*f*i/sr);
        l[i]=s; r[i]= s*(1.0f-width) + 0.3f*width*std::sin(2.f*3.14159265f*(f*1.01f)*i/sr + 1.0f); }
}

int main(){
    std::printf("IME — verificación audio→análisis→clasificación→decisión DSP\n");
    const float sr=48000.f; const int N=320;
    MusicFeatureExtractor fe; MusicIntelligenceEngine eng;
    EXPECT(fe.prepare(sr, 1024), "extractor prepare() acepta SR y bloque válidos");
    EXPECT(!fe.prepare(0.f, 1024), "prepare() rechaza SR inválida (guard)");

    // Señal A: graves dominantes, dinámica (sine 60 Hz con envolvente)
    std::vector<float> al(N),ar(N); genSine(al,ar,sr,60.f,0.2f);
    for(int i=0;i<N;++i){ float env=0.5f+0.5f*std::sin(2.f*3.14159265f*i/(N*0.5f)); al[i]*=env; ar[i]*=env; }
    fe.reset(); fe.processBlock(al.data(), ar.data(), N);
    MusicFeatures fa=fe.features();
    EXPECT(std::isfinite(fa.rms)&&std::isfinite(fa.crestDb), "características A finitas (sin NaN)");
    EXPECT(fa.bassRatio>0.5f, "sine 60 Hz → bassRatio dominante (>0.5)");
    MusicDecision da=eng.decide(fa);
    EXPECT(da.profileIndex>=0 && da.confidence>0.f && da.confidence<=1.f, "decisión A con perfil y confianza en (0,1]");

    // Señal B: agudos brillantes + transitorios (ruido con impulsos)
    std::vector<float> bl(N),br(N);
    for(int i=0;i<N;++i){ float v=((i*1103515245u+12345u)>>16 & 0x7fff)/16383.5f-1.f; bl[i]=0.2f*v; br[i]=0.2f*v; if(i%40==0){bl[i]=0.9f;br[i]=0.9f;} }
    fe.reset(); fe.processBlock(bl.data(), br.data(), N);
    MusicFeatures fb=fe.features();
    MusicDecision db=eng.decide(fb);

    // La decisión DEBE cambiar el DSP de forma distinta según el material
    bool diffProfile = (da.profileIndex!=db.profileIndex);
    bool diffDsp = (std::fabs(da.wfsSpread-db.wfsSpread)>1e-6f) ||
                   (std::fabs(da.dynamicsAmount-db.dynamicsAmount)>1e-6f);
    EXPECT(diffProfile || diffDsp, "materiales distintos → decisión DSP distinta (no es una clasificación vacía)");
    std::printf("  A: style=%s conf=%.2f wfs=%.2f dyn=%.2f | B: style=%s conf=%.2f wfs=%.2f dyn=%.2f\n",
        da.style,da.confidence,da.wfsSpread,da.dynamicsAmount,
        db.style,db.confidence,db.wfsSpread,db.dynamicsAmount);

    // Determinismo + estabilidad en llamadas repetidas (sin degradación de estado)
    MusicDecision da2=eng.decide(fa);
    EXPECT(da2.profileIndex==da.profileIndex && da2.wfsSpread==da.wfsSpread, "decisión determinista y estable en llamadas repetidas");

    // Diferentes tamaños de bloque (robustez). Buffers de origen propios por
    // iteracion, del tamano de blk: reutilizar al/ar (N=320) con blk>320
    // (384, 512) leia fuera del vector -> heap-buffer-overflow bajo ASan
    // (bug del arnes de test, no del extractor: processBlock() es una API de
    // puntero crudo que confia en `frames`, igual que el resto del DSP RT).
    for(int blk : {128, 240, 320, 384, 512}){
        std::vector<float> xl(blk), xr(blk); genSine(xl, xr, sr, 60.f, 0.2f);
        fe.reset(); fe.processBlock(xl.data(), xr.data(), blk);
        MusicFeatures fx=fe.features();
        if(!std::isfinite(fx.rms)) { EXPECT(false,"bloque produce NaN"); }
    }
    EXPECT(true,"distintos tamaños de bloque (128..512) sin NaN");

    // Stereo width: mono → ~0, descorrelacionado → >0.3
    std::vector<float> ml(N,0.3f), mr(N,0.3f); fe.reset(); fe.processBlock(ml.data(),mr.data(),N);
    EXPECT(fe.features().stereoWidth<0.05f, "señal mono idéntica → stereoWidth≈0");

    if(g_fail){ std::printf("FALLARON %d\n", g_fail); return 1; }
    std::printf("IME: TODOS LOS TESTS PASARON.\n"); return 0;
}
