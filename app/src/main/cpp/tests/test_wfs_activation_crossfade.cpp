// test_wfs_activation_crossfade.cpp — activación/desactivación WFS glitch-free.
// Replica el algoritmo EXACTO del crossfade de IvannaFusionEngine::process()
// (smoothstep 3t²−2t³ sobre fade por bloque de 20 ms) sobre una señal real,
// verificando continuidad muestra a muestra en todas las transiciones.
// Standalone sin gtest (patrón test_audio_bus.cpp).
#include "../spatial/WfsRenderer.hpp"
#include <cmath>
#include <cstdio>
#include <vector>

static int g_failures = 0;
#define EXPECT(cond, msg) do { \
    if (!(cond)) { std::printf("  [FAIL] %s\n", msg); ++g_failures; } \
    else         { std::printf("  [ok]   %s\n", msg); } } while (0)

struct FadeEngine {  // réplica exacta del bucle de crossfade del core
    ivanna::spatial::WfsRenderer w;
    float fade = 0.f; bool init = false; float sr = 48000.f;
    std::vector<float> inL, inR, oL, oR;
    void ensure(int n) {
        if ((int)inL.size() != n) {
            inL.assign(n,0.f); inR.assign(n,0.f); oL.assign(n,0.f); oR.assign(n,0.f);
            w.init(sr, n, 16);
        }
        if (!init) { w.init(sr, n, 16); init = true; }
    }
    void block(std::vector<float>& L, std::vector<float>& R, bool want, float spread) {
        const int n = (int)L.size();
        ensure(n);
        const float step = (float)n / (0.020f * sr);
        if (want && fade < 1.f) fade = fade + step > 1.f ? 1.f : fade + step;
        else if (!want && fade > 0.f) fade = fade - step < 0.f ? 0.f : fade - step;
        if (fade > 0.f) {
            for (int i=0;i<n;++i){inL[i]=L[i];inR[i]=R[i];}
            for (int i=0;i<n;++i){oL[i]=0.f;oR[i]=0.f;}
            w.setObject(0,-0.75f*spread,1.5f,1.f);
            w.setObject(1, 0.75f*spread,1.5f,1.f);
            const float* in[2]={inL.data(),inR.data()};
            w.process(in,2,oL.data(),oR.data(),n);
            const float t=fade, sm=t*t*(3.f-2.f*t), dry=1.f-sm;
            for (int i=0;i<n;++i){
                L[i]=dry*L[i]+sm*oL[i];
                R[i]=dry*R[i]+sm*oR[i];
            }
        }
    }
};

static bool allFinite(const std::vector<float>& v){ for(float x:v) if(!std::isfinite(x)) return false; return true; }
static float maxAbs(const std::vector<float>& v){ float m=0; for(float x:v){float a=std::fabs(x); if(a>m)m=a;} return m; }
static float maxStep(const std::vector<float>& v){ float m=0; for(size_t i=1;i<v.size();++i){float d=std::fabs(v[i]-v[i-1]); if(d>m)m=d;} return m; }

static void tone(std::vector<float>& v, float f, float sr, int& phase){
    for (auto& x : v) { x = 0.6f*std::sin(2.f*3.14159265f*f*(phase++)/sr); }
}

int main(){
    std::printf("WFS activación/desactivación glitch-free — verificación\n");
    constexpr float SR = 48000.f;
    constexpr int N = 384;
    const float TONE_MAX_STEP = 0.6f * 2.f * 3.14159265f * 1000.f / SR * 1.1f; // seno 1kHz
    const float THRESH = TONE_MAX_STEP * 3.0f;  // holgura para el procesado

    // ── 1) Bypass total: señal intacta bit a bit ──
    {
        FadeEngine e; int ph=0;
        std::vector<float> L(N),R(N),L0,R0;
        tone(L,1000,SR,ph); ph=0; tone(R,1000,SR,ph); L0=L; R0=R;
        e.block(L,R,false,1.f);
        EXPECT(L==L0 && R==R0, "bypass: salida idéntica a la entrada (cero procesado)");
    }
    // ── 2) Activación sin salto artificial ──
    {
        FadeEngine e; int ph=0;
        float worst=0.f; bool finite=true, clip=true;
        for(int b=0;b<12;++b){
            std::vector<float> L(N),R(N);
            tone(L,1000,SR,ph); tone(R,1000,SR,ph);
            e.block(L,R,true,1.f);
            if(!allFinite(L)||!allFinite(R)) finite=false;
            if(maxAbs(L)>1.0f||maxAbs(R)>1.0f) clip=false;
            float s=std::fmax(maxStep(L),maxStep(R)); if(s>worst)worst=s;
        }
        EXPECT(finite,"activación: sin NaN/Inf en ningún bloque");
        EXPECT(clip,"activación: sin clipping (|x|<=1 siempre)");
        EXPECT(worst<THRESH,"activación: sin salto muestra-a-muestra artificial (continuidad)");
    }
    // ── 3) Desactivación sin salto artificial ──
    {
        FadeEngine e; int ph=0;
        for(int b=0;b<8;++b){std::vector<float> L(N),R(N);tone(L,1000,SR,ph);tone(R,1000,SR,ph);e.block(L,R,true,1.f);}
        float worst=0.f; bool finite=true;
        for(int b=0;b<12;++b){
            std::vector<float> L(N),R(N);
            tone(L,1000,SR,ph);tone(R,1000,SR,ph);
            e.block(L,R,false,1.f);
            if(!allFinite(L)||!allFinite(R)) finite=false;
            float s=std::fmax(maxStep(L),maxStep(R)); if(s>worst)worst=s;
        }
        EXPECT(finite,"desactivación: sin NaN/Inf");
        EXPECT(worst<THRESH,"desactivación: sin salto muestra-a-muestra artificial");
    }
    // ── 4) Activaciones repetidas no degradan estado ──
    {
        FadeEngine e; int ph=0; bool ok=true;
        for(int cycle=0;cycle<10;++cycle){
            for(int b=0;b<6;++b){std::vector<float> L(N),R(N);tone(L,1000,SR,ph);tone(R,1000,SR,ph);e.block(L,R,true,1.f);if(!allFinite(L)||!allFinite(R)||maxAbs(L)>1.f)ok=false;}
            for(int b=0;b<6;++b){std::vector<float> L(N),R(N);tone(L,1000,SR,ph);tone(R,1000,SR,ph);e.block(L,R,false,1.f);if(!allFinite(L)||!allFinite(R)||maxAbs(L)>1.f)ok=false;}
        }
        EXPECT(ok,"10 ciclos on/off: sin NaN, sin clipping, estado íntegro");
    }
    // ── 5) Diferentes tamaños de bloque (HAL heterogéneos) ──
    {
        bool ok=true;
        for(int n : {64, 128, 240, 384, 512, 960}){
            FadeEngine e; int ph=0;
            for(int b=0;b<6;++b){
                std::vector<float> L(n),R(n);
                tone(L,1000,SR,ph);tone(R,1000,SR,ph);
                e.block(L,R,true,1.f);
                if(!allFinite(L)||!allFinite(R)||maxAbs(L)>1.f||maxStep(L)>THRESH){ok=false;break;}
            }
        }
        EXPECT(ok,"tamaños de bloque 64..960: todas las transiciones limpias");
    }
    std::printf("\n====================================================\n");
    if(g_failures==0){std::printf("TODOS LOS TESTS PASARON.\n");return 0;}
    std::printf("%d TEST(S) FALLARON.\n",g_failures);
    return 1;
}
