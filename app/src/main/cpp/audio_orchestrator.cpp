#include <jni.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <algorithm>
#include <array>
#include <fstream>
#include <mutex>
#include <android/log.h>

#include "omega_shared.h"
#include "evolutionary_kernel.h"
#include "dsp/loudness_meter.hpp"  // BS.1770-4 LUFS real (K-weighting + gating)

#define LOG_TAG "AudioOrchestrator"
#define ALOG(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

struct OrchestratorState {

    float dialogGain = 0.f;
    float bassGain = 0.f;
    float widenerWet = 0.f;

    bool manifoldEnabled = false;

    float anti_dolby_speech = 0.f;
    float anti_dolby_music = 0.f;
    float anti_dolby_bass = 0.f;

    float masterGainDb = 0.f;
    float eqGainDb = 0.f;
    float stereoWidth = 0.f;

    float lastLufs = -70.f;
    float lastPeakDbfs = -70.f;
    ivanna::metering::LoudnessMeter loudnessMeter;  // BS.1770-4 real

    float loudness_curve[256]{};

    struct KalmanScalar {
        float q;
        float r;
        float x;
        float p;
    };

    KalmanScalar kalman_loud
        {0.001f,0.1f,0.f,1.f};

    KalmanScalar kalman_trans
        {0.005f,0.2f,0.f,1.f};


    uint8_t active_genome[GENOME_SIZE]{};

    bool genome_valid = false;
    uint32_t genome_generation = 0;


    // NUEVO:
    // memoria del widener M/S mono-safe
    float sideLpState = 0.f;

    // NUEVO:
    // ganancia del manifold suavizada
    float genomeGainSmoothed = 1.f;
};


static OrchestratorState g_orch;
static std::mutex g_orch_mutex;



static void update_anti_dolby(float speech,
                              float music,
                              float bass)
{
    g_orch.anti_dolby_speech =
        std::max(0.f,std::min(1.f,speech));

    g_orch.anti_dolby_music =
        std::max(0.f,std::min(1.f,music));

    g_orch.anti_dolby_bass =
        std::max(0.f,std::min(1.f,bass));
}


static void init_loudness_curve()
{
    static const float freqs[] = {
        20,25,31.5f,40,50,63,80,100,
        125,160,200,250,315,400,
        500,630,800,1000,1250,
        1600,2000,2500,3150,
        4000,5000,6300,8000,
        10000,12500
    };

    static const float levels[] = {
        0,-0.5f,-1.6f,-3.2f,
        -4.1f,-4.8f,-5.4f,
        -5.7f,-5.3f,-4.5f,
        -3.4f,-2.2f,-1.0f,
        0.3f,1.3f,1.8f,
        1.9f,1.8f,1.5f,
        1.0f,0.5f,-0.2f,
        -1.5f,-3.0f,-4.8f,
        -6.5f,-8.2f,-9.7f,
        -10.5f
    };

    constexpr int kN =
        sizeof(freqs)/sizeof(freqs[0]);


    for(int i=0;i<256;i++){

        float freq =
            20.f *
            std::pow(
                2.f,
                i*(std::log2(20000.f/20.f)/255.f)
            );

        int idx=0;

        while(idx<kN-1 &&
              freqs[idx]<freq)
            ++idx;


        if(idx==0)
            g_orch.loudness_curve[i]=levels[0];

        else {

            float t =
              (freq-freqs[idx-1]) /
              (freqs[idx]-freqs[idx-1]);

            g_orch.loudness_curve[i] =
              levels[idx-1] +
              t*(levels[idx]-levels[idx-1]);
        }
    }
}


static void kalman_update(float* x,
                          float* p,
                          float q,
                          float r,
                          float m)
{
    *p += q;

    float k =
        *p /
        (*p+r);

    *x += k*(m-*x);

    *p =
        (1.f-k)*(*p);
}


extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_audio_AudioEngine_nativeSetAntiDolbyScores(
    JNIEnv*, jobject,
    jfloat speech,
    jfloat music,
    jfloat bass)
{
    std::lock_guard<std::mutex> lock(g_orch_mutex);
    update_anti_dolby(speech,music,bass);
}


extern "C" void ivanna_set_anti_dolby_scores(
    float speech,
    float music,
    float bass)
{
    if(!std::isfinite(speech) ||
       !std::isfinite(music) ||
       !std::isfinite(bass))
        return;

    std::lock_guard<std::mutex> lock(g_orch_mutex);
    update_anti_dolby(speech,music,bass);
}


extern "C" void ivanna_set_route_profile(
    float bassBoostDb,
    float dialogBoostDb,
    float widenerMult)
{
    if(!std::isfinite(bassBoostDb) ||
       !std::isfinite(dialogBoostDb) ||
       !std::isfinite(widenerMult))
        return;


    std::lock_guard<std::mutex> lock(g_orch_mutex);

    g_orch.bassGain =
        bassBoostDb;

    g_orch.dialogGain =
        dialogBoostDb;

    g_orch.widenerWet =
        widenerMult;
}


extern "C" void ivanna_set_manifold_enabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(g_orch_mutex);
    g_orch.manifoldEnabled = enabled;
}


extern "C" void ivanna_set_master_gain(float db)
{
    if(!std::isfinite(db))
        return;

    std::lock_guard<std::mutex> lock(g_orch_mutex);

    g_orch.masterGainDb =
        std::clamp(db,-24.f,24.f);
}


extern "C" void ivanna_set_eq_gain(float db)
{
    if(!std::isfinite(db))
        return;

    std::lock_guard<std::mutex> lock(g_orch_mutex);

    g_orch.eqGainDb =
        std::clamp(db,-12.f,12.f);
}


extern "C" void ivanna_set_stereo_width(float width)
{
    if(!std::isfinite(width))
        return;

    std::lock_guard<std::mutex> lock(g_orch_mutex);

    g_orch.stereoWidth =
        std::clamp(width,0.f,1.f);
}


extern "C" float ivanna_get_lufs()
{
    std::lock_guard<std::mutex> lock(g_orch_mutex);
    return g_orch.lastLufs;
}


extern "C" float ivanna_get_peak_dbfs()
{
    std::lock_guard<std::mutex> lock(g_orch_mutex);
    return g_orch.lastPeakDbfs;
}


extern "C" void ivanna_orchestrate(
    float* buffer,
    int samples,
    int channels,
    int sampleRate)
{

    const int sampleRateHz =
        (sampleRate >= 8000 &&
         sampleRate <= 768000)
        ? sampleRate
        : 48000;


    if(buffer == nullptr ||
       samples <= 0)
        return;


    std::lock_guard<std::mutex> lock(g_orch_mutex);



    float rms=0.f;
    float peak=0.f;


    for(int i=0;i<samples;i++){

        float s =
            std::fabs(buffer[i]);

        if(s>peak)
            peak=s;

        rms +=
            buffer[i]*buffer[i];
    }


    rms =
      std::sqrt(rms /
      static_cast<float>(samples));


    kalman_update(
        &g_orch.kalman_loud.x,
        &g_orch.kalman_loud.p,
        0.001f,
        0.1f,
        20.f*std::log10(rms+1e-9f));


    kalman_update(
        &g_orch.kalman_trans.x,
        &g_orch.kalman_trans.p,
        0.005f,
        0.2f,
        peak/(rms+1e-9f));


    // FIX (LUFS real): antes 20*log10(rms) que es dBFS de RMS, no LUFS.
    // LUFS BS.1770-4 requiere filtrado K-weighting (high-shelf 1681 Hz +
    // high-pass 38 Hz) y gating absoluto/relativo. El LoudnessMeter ya
    // implementa todo esto — solo hay que alimentarlo y leer el resultado.
    // Mono: se alimenta el promedio L+R (canal único) que es lo que
    // audio_orchestrator ya calcula para rms y peak.
    g_orch.loudnessMeter.configure((double)sampleRateHz);
    // Alimentar el bloque mono (feedMono duplica a ambos canales con G=1.0)
    {
        // Construir buffer mono temporal: promedio de todos los frames
        // procesados en este bloque (samples / channels)
        const int monoFrames = (channels > 0) ? samples / channels : samples;
        // Stack-safe para bloques de hasta 8192 frames mono
        if (monoFrames > 0 && monoFrames <= 8192) {
            float monoBuf[8192];
            if (channels == 2) {
                for (int i = 0; i < monoFrames; ++i)
                    monoBuf[i] = (buffer[2*i] + buffer[2*i+1]) * 0.5f;
            } else {
                for (int i = 0; i < monoFrames; ++i)
                    monoBuf[i] = buffer[i];
            }
            g_orch.loudnessMeter.feedMono(monoBuf, monoFrames);
        }
    }
    // Leer LUFS integrado real (gated BS.1770-4)
    const float lufs = g_orch.loudnessMeter.integratedLufs();
    g_orch.lastLufs = (lufs > -120.f) ? lufs : (20.f * std::log10(rms + 1e-9f));

    g_orch.lastPeakDbfs = g_orch.loudnessMeter.peakDbfs();



    float dialogLin =
        std::pow(10.f,
        g_orch.dialogGain/20.f);

    float bassLin =
        std::pow(10.f,
        g_orch.bassGain/20.f);

    float masterLin =
        std::pow(10.f,
        (g_orch.masterGainDb+
         g_orch.eqGainDb)/20.f);


    float combined =
        dialogLin*
        bassLin*
        masterLin;


    for(int i=0;i<samples;i++)
        buffer[i]*=combined;



    // Stereo widener M/S mono-safe

    float wetTotal =
        g_orch.widenerWet+
        g_orch.stereoWidth;


    if(channels==2 &&
       wetTotal>0.01f){


        const float fcSide=150.f;


        float aLp =
            1.f -
            std::exp(
              -2.f*3.14159265f*
              fcSide /
              static_cast<float>(sampleRateHz));


        aLp =
          std::clamp(aLp,0.f,1.f);


        float lp =
            g_orch.sideLpState;


        for(int i=0;i+1<samples;i+=2){

            float mid =
              (buffer[i]+buffer[i+1])*0.5f;


            float side =
              (buffer[i]-buffer[i+1])*0.5f;


            lp +=
              aLp*(side-lp);


            float sideHigh =
                side-lp;


            float sideOut =
                lp +
                sideHigh*(1.f+wetTotal);

            float outL = mid + sideOut;
            float outR = mid - sideOut;

            // FIX (soft clip roto): la fórmula anterior era algebraicamente
            // idéntica a hard clip disfrazado:
            //   outL / (1 + outL - 1) = outL / outL = 1.0  ← siempre 1.0
            //   -(-outL / (1 - outL - 1)) = -(-outL / -outL) = -1.0 ← siempre -1.0
            // Es decir, cualquier muestra que superara ±1.0 se recortaba
            // directamente a ±1.0 — exactamente un hard clip, solo
            // disfrazado de fórmula racional. El resultado: espectro de
            // onda cuadrada (armonicos impares infinitos) en vez de
            // saturación suave.
            //
            // CORRECTO: saturador racional con knee en ±0.9 FS.
            // Es idéntico a softCeil() de SafetyLimiter.cpp:
            //   - Identidad para |x| ≤ knee (0.9)
            //   - knee + range*(over/(1+over)) para |x| > knee
            //   → pendiente 1.0 en el knee (C1 continuo, sin escalón)
            //   → asíntota en ceil = 1.0 (nunca supera el techo)
            // Verificación: x=1.5 → over=6, y=0.9+0.1*6/7=0.9857 < 1.0 ✓
            {
                constexpr float kCeil  = 1.0f;
                constexpr float kKnee  = kCeil * 0.9f;   // 0.9
                constexpr float kRange = kCeil - kKnee;   // 0.1
                auto sc = [&](float v) -> float {
                    const float av = v < 0.f ? -v : v;
                    if (av <= kKnee) return v;
                    const float over = (av - kKnee) / kRange;
                    const float y = kKnee + kRange * (over / (1.0f + over));
                    return v < 0.f ? -y : y;
                };
                outL = sc(outL);
                outR = sc(outR);
            }
            // Seguridad numérica dura (residuo FP, NaN): nunca fuera de ±1.0
            if (outL >  1.0f) outL =  1.0f; else if (outL < -1.0f) outL = -1.0f;
            if (outR >  1.0f) outR =  1.0f; else if (outR < -1.0f) outR = -1.0f;

            buffer[i]   = outL;
            buffer[i+1] = outR;
        }


        g_orch.sideLpState=lp;
    }



    if(g_orch.manifoldEnabled &&
       evo_best_fitness()>0.5f){

        uint8_t gen[GENOME_SIZE];

        evo_get_best_genome(
            gen,
            GENOME_SIZE);


        for(int i=0;i<GENOME_SIZE;i++){

            g_orch.active_genome[i] =
              static_cast<uint8_t>(
              (g_orch.active_genome[i]*3+
               gen[i])/4);
        }


        g_orch.genome_valid=true;

        g_orch.genome_generation =
            evo_get_generation();
    }



    if(g_orch.genome_valid){


        double acc=0.0;


        for(int i=0;i<GENOME_SIZE;i++)
            acc+=g_orch.active_genome[i];


        float envAvg =
          static_cast<float>(
          acc/
          (GENOME_SIZE*255.0));


        float gTarget =
          1.f+
          envAvg*0.1f;


        float gStart =
          g_orch.genomeGainSmoothed;


        int chan =
          channels>0 ? channels:1;


        int nFrames =
          samples/chan;


        if(nFrames>0){


            float step =
              (gTarget-gStart)/
              static_cast<float>(nFrames);


            float g=gStart;


            for(int f=0,i=0;
                f<nFrames;
                f++,g+=step){


                for(int c=0;c<chan;c++,i++)
                    buffer[i]*=g;
            }


            g_orch.genomeGainSmoothed =
                gTarget;
        }
    }
}


__attribute__((constructor))
static void init_orchestrator()
{
    init_loudness_curve();
}
