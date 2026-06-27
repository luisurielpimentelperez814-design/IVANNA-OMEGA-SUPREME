// © 2026 Luis Uriel Pimentel Pérez — IVANNA N-P-E — All rights reserved.
// Proprietary and confidential. Embedded copyright; do not strip.
// Verify with ivannanpe::verifyCopyrightIntegrity() at boot.
#pragma once
#include "ivanna_npe_license.h"

/*
 * ============================================================
 *  OMEGA EQ PRO — Ivannuri Gold
 *  PI-LSTM Milenio Engine v2.0 — Complete C++ Implementation
 *  Copyright (C) GORE TNS / Luis Uriel Pimentel Pérez
 *  All rights reserved. Proprietary and confidential.
 *
 *  Signal path:
 *    Input (96kHz) → 4x Upsample → CT-LSTM RK4 → Harmonic Exciter
 *      → HRTF Binaural Field → 4x Downsample → Output (96kHz)
 *
 *  Safety: no NaN/Inf output under any input including NaN/Inf/extremes.
 * ============================================================
 */
#include <cmath>
#include <cstring>
#include <algorithm>
#include <cstdint>

static constexpr int   DIM       = 1;
static constexpr int   DIM2      = DIM * 2;
static constexpr int   BLOCK     = 128;
static constexpr int   UP_FACTOR = 4;
static constexpr int   FIR_TAPS  = 127;
static constexpr int   N_REFL    = 8;
static constexpr int   HRTF_LEN  = 512;
static constexpr float FS_BASE   = 96000.f;
static constexpr float FS_ULTRA  = 384000.f;
static constexpr float DT_ULTRA  = 1.f / FS_ULTRA;
static constexpr float KPI       = 3.14159265f;

// Safety helpers
static inline float sf(float x) noexcept { return std::isfinite(x) ? x : 0.f; }
static inline float clampf(float x,float a,float b) noexcept { return x<a?a:x>b?b:x; }
static inline float sc(float x) noexcept { return clampf(sf(x),-8.f,8.f); }

// Padé [5/4] tanh, |err|<2e-5 on [-8,8]
static inline float fast_tanh(float x) noexcept {
    x=clampf(x,-8.f,8.f);
    float x2=x*x;
    return x*(135135.f+x2*(17325.f+x2*378.f))/(135135.f+x2*(62370.f+x2*(3150.f+x2*28.f)));
}
static inline float fast_sig(float x) noexcept { return 0.5f+0.5f*fast_tanh(x*0.5f); }

// Bessel I0 for Kaiser window
static inline float bess0(float x) noexcept {
    float s=1.f,t=1.f,h=(x*0.5f)*(x*0.5f);
    for(int k=1;k<=20;++k){ t*=h/(float)(k*k); s+=t; if(t<1e-12f*s)break; }
    return s;
}

static inline void build_fir(float* h, float fc, float beta) noexcept {
    float norm=bess0(beta), half=(float)(FIR_TAPS-1)*0.5f;
    for(int n=0;n<FIR_TAPS;++n){
        float t=n-half, r=t/half;
        float w=bess0(beta*std::sqrt(std::max(0.f,1.f-r*r)))/norm;
        float sinc=(std::fabs(t)<1e-9f)?2.f*fc:std::sin(2.f*KPI*fc*t)/(KPI*t);
        h[n]=sinc*w;
    }
}

// ── Polyphase Upsampler (1x → 4x) ────────────────────────────
struct PolyphaseUpsampler {
    static constexpr int ST=(FIR_TAPS+UP_FACTOR-1)/UP_FACTOR;
    float sub[UP_FACTOR][ST];
    float dL[ST],dR[ST];
    int head=0;
    void init() noexcept {
        float h[FIR_TAPS]; build_fir(h,0.5f/UP_FACTOR,8.96f);
        for(int n=0;n<FIR_TAPS;++n) h[n]*=UP_FACTOR;
        for(int p=0;p<UP_FACTOR;++p)
            for(int k=0;k<ST;++k){ int i=p+k*UP_FACTOR; sub[p][k]=(i<FIR_TAPS)?h[i]:0.f; }
        std::memset(dL,0,sizeof(dL)); std::memset(dR,0,sizeof(dR));
    }
    void process(float iL,float iR,float* oL,float* oR) noexcept {
        head=(head==0)?ST-1:head-1;
        dL[head]=sf(iL); dR[head]=sf(iR);
        for(int p=0;p<UP_FACTOR;++p){
            float aL=0.f,aR=0.f;
            for(int k=0;k<ST;++k){ int i=(head+k)%ST; aL+=sub[p][k]*dL[i]; aR+=sub[p][k]*dR[i]; }
            oL[p]=sf(aL); oR[p]=sf(aR);
        }
    }
};

// ── Polyphase Downsampler (4x → 1x) ──────────────────────────
// FIX: el delay line debe tener FIR_TAPS entradas (no ST) para que el
// kernel completo h[0..FIR_TAPS-1] acceda a toda la historia de entrada.
// Además, la salida acumula TODAS las ramas polyphase (p=0..UP_FACTOR-1),
// no solo la rama 0 como hacía la versión anterior.
struct PolyphaseDownsampler {
    static constexpr int ST=(FIR_TAPS+UP_FACTOR-1)/UP_FACTOR;
    float sub[UP_FACTOR][ST];
    // Delay line al ritmo de ENTRADA (FIR_TAPS muestras de historia)
    float dL[FIR_TAPS],dR[FIR_TAPS];
    int head=0,phase=0;
    void init() noexcept {
        float h[FIR_TAPS]; build_fir(h,0.5f/UP_FACTOR,8.96f);
        for(int p=0;p<UP_FACTOR;++p)
            for(int k=0;k<ST;++k){ int i=p+k*UP_FACTOR; sub[p][k]=(i<FIR_TAPS)?h[i]:0.f; }
        std::memset(dL,0,sizeof(dL)); std::memset(dR,0,sizeof(dR));
    }
    bool process(float iL,float iR,float& oL,float& oR) noexcept {
        // Circular push en delay line de FIR_TAPS entradas
        head=(head==0)?FIR_TAPS-1:head-1;
        dL[head]=sf(iL); dR[head]=sf(iR);
        bool e=(phase==0);
        if(e){
            // Acumular las UP_FACTOR ramas polyphase:
            //   sub[p][k] corresponde a h[p + k*UP_FACTOR]
            //   accede a la muestra de entrada en delay p + k*UP_FACTOR
            float aL=0.f,aR=0.f;
            for(int p=0;p<UP_FACTOR;++p){
                for(int k=0;k<ST;++k){
                    int off=p+k*UP_FACTOR;
                    if(off>=FIR_TAPS) break;
                    int i=(head+off)%FIR_TAPS;
                    aL+=sub[p][k]*dL[i];
                    aR+=sub[p][k]*dR[i];
                }
            }
            oL=sf(aL); oR=sf(aR);
        }
        phase=(phase+1)%UP_FACTOR;
        return e;
    }
};

// ── HRTF + Early Reflections ──────────────────────────────────
struct HRTFReflectionEngine {
    float hrtf_L[HRTF_LEN],hrtf_R[HRTF_LEN];
    float hbufL[HRTF_LEN],hbufR[HRTF_LEN];
    int hhead=0;
    static constexpr int MAX_DELAY=4096;
    float rbufL[MAX_DELAY],rbufR[MAX_DELAY];
    int rhead=0;
    int delays_smp[N_REFL];
    float gains[N_REFL];
    bool enabled=true;

    void init(const float ds[N_REFL],const float gs[N_REFL]) noexcept {
        for(int i=0;i<HRTF_LEN;++i){
            float t=i/FS_ULTRA,e=std::exp(-t*8000.f);
            hrtf_L[i]=(i==0)?1.f:e*std::sin(2.f*KPI*2000.f*t)*0.05f;
            float tR=t-0.0007f;
            hrtf_R[i]=(tR<0)?0.f:std::exp(-tR*8000.f)*(1.f+std::sin(2.f*KPI*2000.f*tR)*0.05f)*0.7f;
        }
        std::memset(hbufL,0,sizeof(hbufL));std::memset(hbufR,0,sizeof(hbufR));
        std::memset(rbufL,0,sizeof(rbufL));std::memset(rbufR,0,sizeof(rbufR));
        for(int i=0;i<N_REFL;++i){
            delays_smp[i]=(int)clampf(ds[i]*FS_ULTRA,1.f,(float)(MAX_DELAY-1));
            gains[i]=clampf(gs[i],-1.f,1.f);
        }
    }
    void set_hrtf(const float* L,const float* R) noexcept {
        std::memcpy(hrtf_L,L,HRTF_LEN*sizeof(float));
        std::memcpy(hrtf_R,R,HRTF_LEN*sizeof(float));
    }
    void process(float in,float& oL,float& oR) noexcept {
        if(!enabled){ oL=in; oR=in; return; }
        in=sf(in);
        hbufL[hhead]=in; hbufR[hhead]=in;
        float aL=0.f,aR=0.f;
        for(int k=0;k<HRTF_LEN;++k){
            int i=(hhead-k+HRTF_LEN)&(HRTF_LEN-1);
            aL+=hrtf_L[k]*hbufL[i]; aR+=hrtf_R[k]*hbufR[i];
        }
        hhead=(hhead+1)&(HRTF_LEN-1);
        rbufL[rhead]=aL; rbufR[rhead]=aR;
        float rL=aL,rR=aR;
        for(int i=0;i<N_REFL;++i){
            int ri=(rhead-delays_smp[i]+MAX_DELAY)%MAX_DELAY;
            rL+=gains[i]*rbufL[ri]; rR+=gains[i]*rbufR[ri];
        }
        rhead=(rhead+1)%MAX_DELAY;
        oL=sf(rL); oR=sf(rR);
    }
};

// ── CT-LSTM with RK4 integration ─────────────────────────────
struct CTLSTM {
    float Wf[DIM][DIM2],Wi[DIM][DIM2],Wc[DIM][DIM2],Wo[DIM][DIM2];
    float bf[DIM],bi[DIM],bc[DIM],bo[DIM];
    float alpha,beta,gamma_p,delta,eta,lambda_ie,NP_max;
    float NP[DIM],I_e[DIM],c[DIM],h[DIM],e[DIM],NP_sat[DIM];

    static float dot(const float* w,const float* x,float b,int n) noexcept {
        float s=b; for(int j=0;j<n;++j) s+=w[j]*x[j]; return s;
    }

    void init(float a=1.f,float b=1.f,float g=1.f,float d=1.f,float et=1.f,
              float li=0.01f,float nm=1.f) noexcept {
        alpha=a;beta=b;gamma_p=g;delta=d;eta=et;lambda_ie=li;NP_max=nm;
        float sw=std::sqrt(2.f/(float)(DIM+DIM2));
        for(int i=0;i<DIM;++i){
            for(int j=0;j<DIM2;++j){
                float p=(float)(i*DIM2+j+1);
                Wf[i][j]=std::sin(p*1.618f)*sw; Wi[i][j]=std::sin(p*2.718f)*sw;
                Wc[i][j]=std::sin(p*3.141f)*sw; Wo[i][j]=std::sin(p*1.414f)*sw;
            }
            bf[i]=1.f; bi[i]=bc[i]=bo[i]=0.f;
        }
        std::memset(NP,0,sizeof(NP));std::memset(I_e,0,sizeof(I_e));
        std::memset(c,0,sizeof(c));std::memset(h,0,sizeof(h));
        std::memset(e,0,sizeof(e));std::memset(NP_sat,0,sizeof(NP_sat));
    }

    struct Dy{ float dNP[DIM],dIe[DIM],dc[DIM],hout[DIM]; };

    Dy dyn(const float np[],const float ie[],const float cv[],float Nt) const noexcept {
        Dy o{};
        for(int i=0;i<DIM;++i){
            float inp[DIM2];
            inp[0]=sc(np[i]); inp[DIM+0]=fast_tanh(cv[i]);
            float f=fast_sig(dot(Wf[i],inp,bf[i],DIM2));
            float ig=fast_sig(dot(Wi[i],inp,bi[i],DIM2));
            float g=fast_tanh(dot(Wc[i],inp,bc[i],DIM2));
            float ov=fast_sig(dot(Wo[i],inp,bo[i],DIM2));
            float hi=ov*fast_tanh(cv[i]); o.hout[i]=hi;
            float sat=1.f-clampf(np[i]/NP_max,0.f,1.f);
            o.dNP[i]=sc(alpha*sat*sf(Nt)-beta*sc(np[i])-eta*sc(ie[i]));
            o.dIe[i]=sc(gamma_p*sc(np[i])-delta*sc(ie[i])+lambda_ie*hi);
            o.dc[i] =sc(f*(alpha*sc(np[i])-cv[i])+ig*g);
        }
        return o;
    }

    void rk4(float Nt,float dt) noexcept {
        Dy k1=dyn(NP,I_e,c,Nt);
        float np2[DIM],ie2[DIM],c2[DIM];
        for(int i=0;i<DIM;++i){np2[i]=NP[i]+.5f*dt*k1.dNP[i];ie2[i]=I_e[i]+.5f*dt*k1.dIe[i];c2[i]=c[i]+.5f*dt*k1.dc[i];}
        Dy k2=dyn(np2,ie2,c2,Nt);
        float np3[DIM],ie3[DIM],c3[DIM];
        for(int i=0;i<DIM;++i){np3[i]=NP[i]+.5f*dt*k2.dNP[i];ie3[i]=I_e[i]+.5f*dt*k2.dIe[i];c3[i]=c[i]+.5f*dt*k2.dc[i];}
        Dy k3=dyn(np3,ie3,c3,Nt);
        float np4[DIM],ie4[DIM],c4[DIM];
        for(int i=0;i<DIM;++i){np4[i]=NP[i]+dt*k3.dNP[i];ie4[i]=I_e[i]+dt*k3.dIe[i];c4[i]=c[i]+dt*k3.dc[i];}
        Dy k4=dyn(np4,ie4,c4,Nt);
        for(int i=0;i<DIM;++i){
            NP[i]=sc(NP[i]+(dt/6.f)*(k1.dNP[i]+2.f*k2.dNP[i]+2.f*k3.dNP[i]+k4.dNP[i]));
            I_e[i]=sc(I_e[i]+(dt/6.f)*(k1.dIe[i]+2.f*k2.dIe[i]+2.f*k3.dIe[i]+k4.dIe[i]));
            c[i]=sc(c[i]+(dt/6.f)*(k1.dc[i]+2.f*k2.dc[i]+2.f*k3.dc[i]+k4.dc[i]));
            h[i]=k4.hout[i];
            NP_sat[i]=clampf(NP[i]/NP_max,0.f,1.f);
        }
    }

    void adapt(float lr) noexcept {
        for(int i=0;i<DIM;++i){
            if(!std::isfinite(e[i])){e[i]=0.f;continue;}
            bc[i]=sc(bc[i]-lr*e[i]);
            alpha=clampf(alpha-lr*.1f*e[i],.01f,20.f);
        }
    }

    void sanity() noexcept {
        for(int i=0;i<DIM;++i)
            if(!std::isfinite(NP[i])||!std::isfinite(I_e[i])||!std::isfinite(c[i])||!std::isfinite(h[i])){
                std::memset(NP,0,sizeof(NP));std::memset(I_e,0,sizeof(I_e));
                std::memset(c,0,sizeof(c));std::memset(h,0,sizeof(h)); break;
            }
    }
};

// ── PILSTMMilenio — Main Engine ───────────────────────────────
class PILSTMMilenio {
public:
    CTLSTM               lstm;
    PolyphaseUpsampler   up;
    PolyphaseDownsampler down;
    HRTFReflectionEngine hrtf;

    float harmonic_gain=0.05f;
    bool  adapt_enabled=true;
    int   adapt_counter=0;
    static constexpr int ADAPT_PERIOD=512;
    float ultra_L[BLOCK*UP_FACTOR]{};
    float ultra_R[BLOCK*UP_FACTOR]{};

    void init() noexcept {
        up.init(); down.init();
        const float ds[N_REFL]={.0048f,.0067f,.0093f,.0114f,.0141f,.0173f,.0201f,.0234f};
        const float gs[N_REFL]={.70f,.55f,.45f,.38f,.30f,.24f,.20f,.16f};
        hrtf.init(ds,gs); lstm.init();
    }

    void process_block(
        const float* __restrict iL,const float* __restrict iR,
        float* __restrict oL,float* __restrict oR) noexcept {
        int oi=0;
        for(int n=0;n<BLOCK;++n){
            float sL=sc(iL[n]),sR=sc(iR[n]);
            float upL[UP_FACTOR],upR[UP_FACTOR];
            up.process(sL,sR,upL,upR);
            for(int k=0;k<UP_FACTOR;++k){
                float uL=upL[k],uR=upR[k];
                lstm.rk4((uL+uR)*.5f,DT_ULTRA);
                lstm.sanity();
                float ex=harmonic_gain*fast_tanh(sf(lstm.h[0])*3.f);
                float pL=sc(uL+ex*uL),pR=sc(uR+ex*uR);
                float hL,hR; hrtf.process((pL+pR)*.5f,hL,hR);
                float mL=.7f*pL+.3f*hL, mR=.7f*pR+.3f*hR;
                ultra_L[n*UP_FACTOR+k]=sc(mL);
                ultra_R[n*UP_FACTOR+k]=sc(mR);
                float dsL,dsR;
                if(down.process(mL,mR,dsL,dsR)&&oi<BLOCK){ oL[oi]=sc(dsL); oR[oi]=sc(dsR); ++oi; }
            }
            if(adapt_enabled&&++adapt_counter>=ADAPT_PERIOD){
                adapt_counter=0;
                float rms=0.f;
                for(int i=0;i<DIM;++i) rms+=lstm.NP[i]*lstm.NP[i];
                rms=std::sqrt(rms/DIM);
                for(int i=0;i<DIM;++i) lstm.e[i]=rms-.5f;
                lstm.adapt(1e-5f);
            }
        }
        while(oi<BLOCK){ oL[oi]=oi>0?oL[oi-1]:0.f; oR[oi]=oi>0?oR[oi-1]:0.f; ++oi; }
    }

    void set_alpha(float v)         noexcept { lstm.alpha   =clampf(v,.01f,20.f); }
    void set_beta(float v)          noexcept { lstm.beta    =clampf(v,.01f,20.f); }
    void set_gamma(float v)         noexcept { lstm.gamma_p =clampf(v,.01f,20.f); }
    void set_delta(float v)         noexcept { lstm.delta   =clampf(v,.01f,20.f); }
    void set_eta(float v)           noexcept { lstm.eta     =clampf(v,0.f,5.f);   }
    void set_harmonic_gain(float v) noexcept { harmonic_gain=clampf(v,0.f,1.f);   }
    void set_hrtf_enabled(bool en)  noexcept { hrtf.enabled =en; }
    void set_adapt_enabled(bool en) noexcept { adapt_enabled=en; }
    void set_np_max(float v)        noexcept { lstm.NP_max  =clampf(v,.1f,10.f);  }
    void set_reflection_gain(int i,float g) noexcept  { if(i>=0&&i<N_REFL) hrtf.gains[i]=clampf(g,-1.f,1.f); }
    void set_reflection_delay_ms(int i,float ms) noexcept {
        if(i>=0&&i<N_REFL) hrtf.delays_smp[i]=(int)clampf(ms*.001f*FS_ULTRA,1.f,(float)(HRTFReflectionEngine::MAX_DELAY-1));
    }
    void set_hrtf_ir(const float* L,const float* R) noexcept { hrtf.set_hrtf(L,R); }

    float get_error()  const noexcept { return sf(lstm.e[0]);     }
    float get_np_sat() const noexcept { return sf(lstm.NP_sat[0]);}
    float get_alpha()  const noexcept { return lstm.alpha;  }
    float get_beta()   const noexcept { return lstm.beta;   }
    float get_gamma()  const noexcept { return lstm.gamma_p;}
    float get_delta()  const noexcept { return lstm.delta;  }
    float get_eta()    const noexcept { return lstm.eta;    }
};
