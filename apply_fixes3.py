# -*- coding: utf-8 -*-
import io, os, shutil

def rep(path, old, new, label):
    with io.open(path, "r", encoding="utf-8") as f:
        s = f.read()
    if old not in s:
        print("SKIP (no match): " + label)
        return False
    s = s.replace(old, new, 1)
    with io.open(path, "w", encoding="utf-8") as f:
        f.write(s)
    print("OK: " + label)
    return True

CF_HEADER = "app/src/main/cpp/control_frame.hpp"
JNI = "app/src/main/cpp/jni/ivanna_omega_jni.cpp"

# 0. Crear control_frame.hpp si no existe (copiar desde control_frame.hpp
#    que debes tener en la MISMA carpeta donde corres este script)
if not os.path.exists(CF_HEADER):
    src = "control_frame.hpp"
    if os.path.exists(src):
        shutil.copy(src, CF_HEADER)
        print("OK: creado " + CF_HEADER)
    else:
        print("FALTA: coloca control_frame.hpp junto a este script antes de correrlo")
else:
    print("SKIP (ya existe): " + CF_HEADER)

# 1. Include + globals + helper apply_pending_control_frame
rep(JNI,
'''#include "../pd_engine.hpp"

#define LOG_TAG "IVANNA-JNI"''',
'''#include "../pd_engine.hpp"
#include "../control_frame.hpp"

#define LOG_TAG "IVANNA-JNI"''',
"include control_frame.hpp")

rep(JNI,
'''static DSPParams      g_params;
static std::atomic<bool> g_initialized{false};''',
'''static DSPParams      g_params;
static std::atomic<bool> g_initialized{false};

// FIX audio-cleanup (thread-safety): antes el hilo JNI mutaba g_eq/g_comp/
// g_exciter/g_widener/g_gain directamente vía setParams() mientras el hilo
// de audio los procesaba en paralelo (race real, no solo percibida). Ahora
// el hilo JNI solo publica un ControlFrame inmutable; el hilo de audio lo
// consume una vez al inicio de cada bloque.
static ControlFrame      g_staging_frame;
static ControlFrameBus   g_control_bus;
static thread_local uint64_t t_last_seq_dspbridge = 0;
static thread_local uint64_t t_last_seq_nativelib  = 0;

static inline void apply_pending_control_frame(uint64_t& lastSeq) {
    ControlFrame f;
    if (!g_control_bus.consumeIfNewer(f, lastSeq)) return;
    DSPParams p = f.toDSPParams(g_params.sampleRate);
    g_params = p;
    g_eq.setParams(p);
    g_comp.setParams(p);
    g_exciter.setParams(p);
    g_widener.setParams(p);
    g_gain.setParams(p);
    g_pd.set_nho_alpha(f.alpha);
    g_pd.set_nho_beta(f.beta);
    g_pd.set_nho_wet(f.wet * 0.5f);
}''',
"globals + apply_pending_control_frame()")

# 2. nativeInit (DSPBridge): publica frame inicial en vez de setParams directo
rep(JNI,
'''    g_params.sampleRate = (uint32_t)sr;
    g_eq.setParams(g_params);
    g_comp.setParams(g_params);
    g_exciter.setParams(g_params);
    g_widener.setParams(g_params);
    g_gain.setParams(g_params);
    g_pd.init((uint32_t)sr);
    g_pd.start_evo_thread();
    g_initialized.store(true, std::memory_order_release);
    LOGI("OPE initialized @ %d Hz (EvolutionaryKernel online)", sr);
}''',
'''    g_params.sampleRate = (uint32_t)sr;
    g_staging_frame = ControlFrame{};
    g_control_bus.publish(g_staging_frame);
    g_pd.init((uint32_t)sr);
    uint64_t seq0 = 0;
    apply_pending_control_frame(seq0);
    t_last_seq_dspbridge = seq0;
    t_last_seq_nativelib  = seq0;
    g_pd.start_evo_thread();
    g_initialized.store(true, std::memory_order_release);
    LOGI("OPE initialized @ %d Hz (EvolutionaryKernel online, control-frame)", sr);
}''',
"DSPBridge_nativeInit: publica ControlFrame en vez de setParams directo")

# 3. nativeSetParams (DSPBridge, floats individuales): solo publica, no toca DSP
rep(JNI,
'''    g_params.drive = drive; g_params.wet = wet;   g_params.mix = mix;
    g_params.alpha = alpha; g_params.beta = beta; g_params.gamma = gamma_v;
    g_params.freq  = freq;  g_params.resonance = resonance;
    g_params.low   = low;   g_params.mid = mid;   g_params.high = high;
    g_params.presence = presence; g_params.master = master;
    g_eq.setParams(g_params);
    g_comp.setParams(g_params);
    g_exciter.setParams(g_params);
    g_widener.setParams(g_params);
    g_gain.setParams(g_params);
    g_pd.set_nho_alpha(alpha);
    g_pd.set_nho_beta(beta);
    g_pd.set_nho_wet(wet * 0.5f);
}''',
'''    // FIX audio-cleanup: ya NO toca g_eq/g_comp/... aqui (hilo JNI). Solo
    // construye y publica el frame; el hilo de audio lo aplica el.
    g_staging_frame.drive = drive; g_staging_frame.wet = wet; g_staging_frame.mix = mix;
    g_staging_frame.alpha = alpha; g_staging_frame.beta = beta; g_staging_frame.gamma_v = gamma_v;
    g_staging_frame.freq  = freq;  g_staging_frame.resonance = resonance;
    g_staging_frame.low   = low;   g_staging_frame.mid = mid;   g_staging_frame.high = high;
    g_staging_frame.presence = presence; g_staging_frame.master = master;
    g_control_bus.publish(g_staging_frame);
}''',
"DSPBridge_nativeSetParams: publish-only")

# 4. nativeProcess: consumir frame al inicio del bloque
rep(JNI,
'''    if (!g_initialized.load(std::memory_order_acquire)) return;
    if (!buf || nFrames <= 0) return;
    const int n = std::min((int)nFrames, 2048);''',
'''    if (!g_initialized.load(std::memory_order_acquire)) return;
    if (!buf || nFrames <= 0) return;
    apply_pending_control_frame(t_last_seq_dspbridge);
    const int n = std::min((int)nFrames, 2048);''',
"DSPBridge_nativeProcess: consume ControlFrame al inicio del bloque")

# 5. nativeInitDSP (IvannaNativeLib): publica frame inicial
rep(JNI,
'''    g_params.sampleRate = (uint32_t)sr;
    g_eq.setParams(g_params); g_comp.setParams(g_params);
    g_exciter.setParams(g_params); g_widener.setParams(g_params);
    g_gain.setParams(g_params);
    g_pd.init((uint32_t)sr);
    g_pd.start_evo_thread();
    g_initialized.store(true, std::memory_order_release);
    LOGI("IvannaNativeLib DSP @ %d Hz (EvolutionaryKernel online)", sr);
    return JNI_TRUE;
}''',
'''    g_params.sampleRate = (uint32_t)sr;
    g_staging_frame = ControlFrame{};
    g_control_bus.publish(g_staging_frame);
    g_pd.init((uint32_t)sr);
    uint64_t seq0 = 0;
    apply_pending_control_frame(seq0);
    t_last_seq_dspbridge = seq0;
    t_last_seq_nativelib  = seq0;
    g_pd.start_evo_thread();
    g_initialized.store(true, std::memory_order_release);
    LOGI("IvannaNativeLib DSP @ %d Hz (EvolutionaryKernel online, control-frame)", sr);
    return JNI_TRUE;
}''',
"IvannaNativeLib_nativeInitDSP: publica ControlFrame en vez de setParams directo")

# 6. nativeProcessBlock: consumir frame al inicio del bloque
rep(JNI,
'''    if (!g_initialized.load(std::memory_order_acquire) || frames <= 0) return;

    float lBuf[2048], rBuf[2048], oL[2048], oR[2048];''',
'''    if (!g_initialized.load(std::memory_order_acquire) || frames <= 0) return;
    apply_pending_control_frame(t_last_seq_nativelib);

    float lBuf[2048], rBuf[2048], oL[2048], oR[2048];''',
"IvannaNativeLib_nativeProcessBlock: consume ControlFrame al inicio del bloque")

# 7. nativeSetParams (IvannaNativeLib, array-based): solo publica
rep(JNI,
'''    if (n>=13) g_params.master    = p[12];
    env->ReleaseFloatArrayElements(params, p, JNI_ABORT);
    g_eq.setParams(g_params); g_comp.setParams(g_params);
    g_exciter.setParams(g_params); g_widener.setParams(g_params);
    g_gain.setParams(g_params);
}''',
'''    if (n>=13) g_staging_frame.master    = p[12];
    env->ReleaseFloatArrayElements(params, p, JNI_ABORT);
    g_control_bus.publish(g_staging_frame);
}''',
"IvannaNativeLib_nativeSetParams: publish-only")

rep(JNI,
'''    if (n>=1)  g_params.drive     = p[0];
    if (n>=2)  g_params.wet       = p[1];
    if (n>=3)  g_params.mix       = p[2];
    if (n>=4)  g_params.alpha     = p[3];
    if (n>=5)  g_params.beta      = p[4];
    if (n>=6)  g_params.gamma     = p[5];
    if (n>=7)  g_params.freq      = p[6];
    if (n>=8)  g_params.resonance = p[7];
    if (n>=9)  g_params.low       = p[8];
    if (n>=10) g_params.mid       = p[9];
    if (n>=11) g_params.high      = p[10];
    if (n>=12) g_params.presence  = p[11];''',
'''    if (n>=1)  g_staging_frame.drive     = p[0];
    if (n>=2)  g_staging_frame.wet       = p[1];
    if (n>=3)  g_staging_frame.mix       = p[2];
    if (n>=4)  g_staging_frame.alpha     = p[3];
    if (n>=5)  g_staging_frame.beta      = p[4];
    if (n>=6)  g_staging_frame.gamma_v   = p[5];
    if (n>=7)  g_staging_frame.freq      = p[6];
    if (n>=8)  g_staging_frame.resonance = p[7];
    if (n>=9)  g_staging_frame.low       = p[8];
    if (n>=10) g_staging_frame.mid       = p[9];
    if (n>=11) g_staging_frame.high      = p[10];
    if (n>=12) g_staging_frame.presence  = p[11];''',
"IvannaNativeLib_nativeSetParams: campos individuales -> g_staging_frame")

print("DONE")
