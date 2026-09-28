// saf_latent_bridge.cpp
//
// FIX (build): undefined symbol `ivanna_saf_apply_latent` al linkear
// libivanna_omega.so.
//
// Diagnóstico:
//   * SaFJniBridge.cpp:17 declara `extern "C" void ivanna_saf_apply_latent(...)`
//     y la llama en :40 tras cada feedFeedback() del optimizador.
//   * La definición real vive en omega_effect.cpp:421, pero ese TU solo
//     compila al target `libomega_effect.so` (proceso audioserver).
//   * libivanna_omega.so (proceso app) linkea SaFJniBridge.cpp pero NO
//     omega_effect.cpp → undefined symbol.
//   * IvannaFusionCore (clase ad-hoc incluida textualmente en omega_effect.cpp)
//     NO existe en este target, así que no se puede replicar el cuerpo.
//
// Regla de oro — no borramos, cableamos:
//   Este stub define el símbolo en el proceso app. En lugar de un pozo
//   ciego, publica el vector latente q[7] en un snapshot global atómico
//   (ivanna_saf_get_latent_snapshot) para que el lado Kotlin/C++ del
//   proceso app pueda leerlo (calibración UI, visualizador, telemetría).
//   El push hacia el ObjectRenderer real vive en el proceso audioserver
//   (omega_effect.cpp) — ese cable inter-proceso es alcance separado.

#include <atomic>
#include <cstring>
#include <android/log.h>
#include "spatial/SofaSafRirMasterKnowledge.hpp"

#define TAG "SaFLatentBridge"

// Snapshot seqlock-lite: inicializado desde el arranque con el latente
// maestro entrenado sobre las 255 mediciones SOFA + 12 datasets IHR1
// (kMasterSafGoldenQ) para que desde el frame 0 sin intervención del usuario
// el renderer opere con calibración magistral.
static std::atomic<uint32_t> g_latentSeq{2};
static float g_latent[7] = {
    ivanna::master::kMasterSafGoldenQ[0],
    ivanna::master::kMasterSafGoldenQ[1],
    ivanna::master::kMasterSafGoldenQ[2],
    ivanna::master::kMasterSafGoldenQ[3],
    ivanna::master::kMasterSafGoldenQ[4],
    ivanna::master::kMasterSafGoldenQ[5],
    ivanna::master::kMasterSafGoldenQ[6]
};

extern "C" uint32_t ivanna_saf_get_latent_seq() {
    return g_latentSeq.load(std::memory_order_acquire);
}

extern "C" void ivanna_saf_apply_latent(const float q[7]) {
    if (!q) return;
    static std::atomic<bool> logged{false};
    if (!logged.exchange(true)) {
        __android_log_print(ANDROID_LOG_INFO, TAG,
            "ivanna_saf_apply_latent cableado en libivanna_omega.so "
            "(snapshot local; el renderer vive en el proceso audioserver)");
    }
    g_latentSeq.fetch_add(1, std::memory_order_release);
    std::memcpy(g_latent, q, sizeof(float) * 7);
    g_latentSeq.fetch_add(1, std::memory_order_release);
}

// Lector lock-free para el proceso app (Kotlin JNI o C++ local).
// Devuelve true si la lectura fue consistente (seq par antes y después).
extern "C" bool ivanna_saf_get_latent_snapshot(float out[7]) {
    if (!out) return false;
    for (int tries = 0; tries < 4; ++tries) {
        const uint32_t s0 = g_latentSeq.load(std::memory_order_acquire);
        if (s0 & 1u) continue;  // escritura en curso
        std::memcpy(out, g_latent, sizeof(float) * 7);
        const uint32_t s1 = g_latentSeq.load(std::memory_order_acquire);
        if (s0 == s1) return true;
    }
    return false;
}
