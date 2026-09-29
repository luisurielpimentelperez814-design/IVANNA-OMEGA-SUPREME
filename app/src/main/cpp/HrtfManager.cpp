#include "HrtfManager.hpp"
#include "spatial/SofaSafRirMasterKnowledge.hpp"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace Ivanna {

HrtfManager::HrtfManager() {
    const char* bootCandidates[] = {
        "/data/adb/ivanna_omega/hrtf_dataset.ihr1",
        "/system/etc/ivanna_omega/hrtf/kemar.ihr1",
        "/data/adb/ivanna_omega/hrtf/kemar.ihr1",
        "/data/data/com.ivanna.omega/files/ivanna_omega/hrtf/kemar.ihr1",
        "/data/user/0/com.ivanna.omega/files/ivanna_omega/hrtf/kemar.ihr1"
    };
    for (const char* p : bootCandidates) {
        if (loadFromDataset(p)) break;
    }
    if (!m_datasetLoaded) {
        synthesizeHrtf(0.0f, 0.0f, 0.0f, 0);
        synthesizeHrtf(0.0f, 0.0f, 0.0f, 1);
    }

    for (size_t i = 0; i < BLOCK_SIZE + HRTF_TAPS; ++i) {
        m_histL[i] = 0.0f;
        m_histR[i] = 0.0f;
    }

    // Inicializar coeficientes de crossfade
    m_xfadePos  = 0;
    m_xfading   = false;
    m_wetEnv.configure(48000.0f, 8.0f, 18.0f, 35.0f);
    m_wetEnv.setImmediate(1.0f);
    ivanna::dsp::HrtfConvolutionConfig cfg;
    cfg.sample_rate_in = 48000;
    cfg.sample_rate_out = 768000;
    cfg.hrtf_filter_length = 512;
    cfg.block_size = BLOCK_SIZE;
    cfg.num_azimuth_bins = 360;
    cfg.num_elevation_bins = 180;
    cfg.use_fft_convolution = true;
    m_fastRpcClient.initialize(cfg);
}

void HrtfManager::synthesizeHrtf(float yaw, float pitch, float roll, int bank) {
    (void)roll;
    const float eff_azimuth = -yaw;
    const float theta = eff_azimuth;
    const float phi   = pitch;
    const float geodesic_dist = std::acos(
        std::max(-1.0f, std::min(1.0f, std::cos(phi) * std::cos(theta)))
    );
    const float riemannian_scale =
        1.0f + m_intrinsicCurvature.load(std::memory_order_relaxed) * std::sin(geodesic_dist);

    const float ild = std::clamp(std::sin(theta) * riemannian_scale * 0.25f, -0.45f, 0.45f);
    const int itdSamples = std::clamp(
        static_cast<int>(std::round(std::sin(theta) * 18.0f * riemannian_scale)),
        -24, 24
    );

    std::memset(m_hrtfLL[bank], 0, HRTF_TAPS * sizeof(float));
    std::memset(m_hrtfRR[bank], 0, HRTF_TAPS * sizeof(float));
    std::memset(m_hrtfLR[bank], 0, HRTF_TAPS * sizeof(float));
    std::memset(m_hrtfRL[bank], 0, HRTF_TAPS * sizeof(float));

    // Construir HRIR causal de 128 taps desde la variedad maestra SOFA + SAF (p0 + V * q_master)
    constexpr int kSofaLen = ivanna::master::kMasterHrirLen;
    float baseL[kSofaLen]{};
    float baseR[kSofaLen]{};
    float energyL = 1e-8f, energyR = 1e-8f;
    for (int n = 0; n < kSofaLen; ++n) {
        float l = ivanna::master::kMasterSofaP0[n];
        float r = ivanna::master::kMasterSofaP0[kSofaLen + n];
        for (int k = 0; k < ivanna::master::kMasterSafK; ++k) {
            const float qk = ivanna::master::kMasterSafGoldenQ[k];
            l += qk * ivanna::master::kMasterSofaPcaV[k][n];
            r += qk * ivanna::master::kMasterSofaPcaV[k][kSofaLen + n];
        }
        baseL[n] = l;
        baseR[n] = r;
        energyL += l * l;
        energyR += r * r;
    }
    const float normL = (1.0f - ild) / std::sqrt(energyL);
    const float normR = (1.0f + ild) / std::sqrt(energyR);
    const int delayL = std::max(0, itdSamples);
    const int delayR = std::max(0, -itdSamples);

    // Almacenar en orden causal invertido [HRTF_TAPS - 1 - tap] porque processBinauralScene
    // indexa m_histL[i + t] donde t = HRTF_TAPS - 1 es la muestra actual x[n].
    for (int n = 0; n < kSofaLen; ++n) {
        const int tapL = n + delayL;
        const int tapR = n + delayR;
        if (tapL < static_cast<int>(HRTF_TAPS)) {
            m_hrtfLL[bank][HRTF_TAPS - 1 - tapL] = baseL[n] * normL;
        }
        if (tapR < static_cast<int>(HRTF_TAPS)) {
            m_hrtfRR[bank][HRTF_TAPS - 1 - tapR] = baseR[n] * normR;
        }
        // Crossfeed acústico natural de sombra cefálica (~14 muestras = 290 us)
        const int crossTapL = tapL + 14;
        const int crossTapR = tapR + 14;
        if (crossTapL < static_cast<int>(HRTF_TAPS)) {
            m_hrtfRL[bank][HRTF_TAPS - 1 - crossTapL] = baseL[n] * normL * 0.12f;
        }
        if (crossTapR < static_cast<int>(HRTF_TAPS)) {
            m_hrtfLR[bank][HRTF_TAPS - 1 - crossTapR] = baseR[n] * normR * 0.12f;
        }
    }
}

void HrtfManager::setHeadPose(float yaw, float pitch, float roll) {
    // FIX (cableado binaural, FASE 8): persistir la pose real en grados,
    // incondicionalmente — antes solo se calculaba azDeg dentro del branch
    // m_datasetLoaded para posicionar el dataset medido, y se perdía justo
    // después de usarse. Cualquier consumidor de la posición real vigente
    // (p.ej. el path FastRPC/Hexagon en processBinauralScene) necesita
    // poder leerla sin importar si hay dataset cargado o síntesis analítica.
    constexpr float kRadToDeg = 180.f / 3.14159265f;
    const float safBias = m_safAzimuthBias.load(std::memory_order_relaxed);
    m_currentAzimuthDeg.store((yaw * kRadToDeg) + safBias, std::memory_order_relaxed);
    m_currentElevationDeg.store(pitch * kRadToDeg, std::memory_order_relaxed);

    // FIX (clic en banco-switch): prepara el banco inactivo y activa crossfade.
    // processBinauralScene mezcla los dos bancos durante XFADE_FRAMES bloques.
    const int inactive = 1 - m_activeBank.load(std::memory_order_relaxed);
    if (m_datasetLoaded) {
        const float azDeg = (yaw * kRadToDeg) + safBias;
        loadFromDatasetAtAzimuth(azDeg, inactive);
    } else {
        synthesizeHrtf(yaw, pitch, roll, inactive);
    }
    // Señalizar crossfade — processBinauralScene lo lee de forma lock-free
    m_pendingBank.store(inactive, std::memory_order_release);
    m_xfadeTrigger.store(true,  std::memory_order_release);
}

void HrtfManager::processBinauralScene(Ivanna::AudioBuffer* buffer) {
    if (m_fastRpcClient.isDSPReady()) {
        // FIX (cableado binaural, FASE 8): azimuth/elevation ya NO se
        // envían fijos en cero — se leen de la última pose real recibida
        // por setHeadPose() (yaw/pitch del sensor + sesgo SAF), la misma
        // fuente de verdad que usa el path CPU de más abajo. Sin este fix,
        // en cualquier dispositivo con Hexagon DSP activo el binaural
        // quedaba congelado siempre al centro, sin importar movimiento de
        // cabeza ni ajuste espacial del optimizer SAF.
        ivanna::dsp::SpatialPosition pos;
        pos.azimuth = m_currentAzimuthDeg.load(std::memory_order_relaxed);
        pos.elevation = m_currentElevationDeg.load(std::memory_order_relaxed);
        pos.distance = 1.0f;  // sin modelo de distancia en el motor actual — campo lejano de referencia
        m_fastRpcClient.delegateBinauralConvolution(
            buffer->left, buffer->right,
            buffer->left, buffer->right,
            pos, BLOCK_SIZE
        );
        return;
    }
    // ── Crossfade lock-free ──────────────────────────────────────────────────
    // Si hay un banco pendiente: inicia crossfade suave sin clic.
    // XFADE_FRAMES bloques de fundido cruzado (coseno)
    if (m_xfadeTrigger.exchange(false, std::memory_order_acq_rel)) {
        m_pendingBankLocal = m_pendingBank.load(std::memory_order_acquire);
        m_xfading  = true;
        m_xfadePos = 0;
    }

    const int bankA = m_activeBank.load(std::memory_order_relaxed);
    const int bankB = m_pendingBankLocal;

    // Peso de crossfade [0..1]: 0 = solo bankA, 1 = solo bankB
    float xfadeW = 0.0f;
    bool  doXfade = m_xfading;
    if (doXfade) {
        xfadeW = static_cast<float>(m_xfadePos + 1) / static_cast<float>(XFADE_FRAMES);
        // Curva coseno para evitar bump de energía
        xfadeW = 0.5f * (1.0f - std::cos(xfadeW * 3.14159265f));
        m_xfadePos++;
        if (m_xfadePos >= XFADE_FRAMES) {
            m_activeBank.store(bankB, std::memory_order_release);
            m_xfading  = false;
            m_xfadePos = 0;
            xfadeW     = 1.0f;
            doXfade    = false;
        }
    }

    // FIX DAC USB-C: leer targetWet una sola vez por bloque y aplicar rampa por muestra
    // con SupremeTransitionEnvelope para evitar clics cuando wet salta entre 0 y 1.
    const float targetWet = std::clamp(m_wetDry.load(std::memory_order_relaxed), 0.0f, 1.0f);

    // ── Ingresar muestras al historial ───────────────────────────────────────
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        m_histL[HRTF_TAPS - 1 + i] = buffer->left[i];
        m_histR[HRTF_TAPS - 1 + i] = buffer->right[i];
    }

    // Bypass completo únicamente cuando la envolvente de transición ha llegado a cero
    if (!m_wetEnv.beginBlock(targetWet)) {
        std::memmove(m_histL, m_histL + BLOCK_SIZE, (HRTF_TAPS - 1) * sizeof(float));
        std::memmove(m_histR, m_histR + BLOCK_SIZE, (HRTF_TAPS - 1) * sizeof(float));
        return;
    }

    // ── Convolución HRTF ─────────────────────────────────────────────────────
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        const float wet = m_wetEnv.nextSample();
        const float dry = 1.0f - wet;
        float32x4_t aLL = vdupq_n_f32(0.f), aLR = vdupq_n_f32(0.f);
        float32x4_t aRR = vdupq_n_f32(0.f), aRL = vdupq_n_f32(0.f);

        for (size_t t = 0; t < HRTF_TAPS; t += 4) {
            const float32x4_t xL  = vld1q_f32(&m_histL[i + t]);
            const float32x4_t xR  = vld1q_f32(&m_histR[i + t]);
            aLL = vmlaq_f32(aLL, vld1q_f32(&m_hrtfLL[bankA][t]), xL);
            aLR = vmlaq_f32(aLR, vld1q_f32(&m_hrtfLR[bankA][t]), xL);
            aRR = vmlaq_f32(aRR, vld1q_f32(&m_hrtfRR[bankA][t]), xR);
            aRL = vmlaq_f32(aRL, vld1q_f32(&m_hrtfRL[bankA][t]), xR);
        }

        auto hsum = [](float32x4_t v) {
            return vgetq_lane_f32(v,0)+vgetq_lane_f32(v,1)+
                   vgetq_lane_f32(v,2)+vgetq_lane_f32(v,3);
        };

        float outL = hsum(aLL) + hsum(aRL);
        float outR = hsum(aRR) + hsum(aLR);

        if (doXfade) {
            float32x4_t bLL = vdupq_n_f32(0.f), bLR = vdupq_n_f32(0.f);
            float32x4_t bRR = vdupq_n_f32(0.f), bRL = vdupq_n_f32(0.f);
            for (size_t t = 0; t < HRTF_TAPS; t += 4) {
                const float32x4_t xL = vld1q_f32(&m_histL[i + t]);
                const float32x4_t xR = vld1q_f32(&m_histR[i + t]);
                bLL = vmlaq_f32(bLL, vld1q_f32(&m_hrtfLL[bankB][t]), xL);
                bLR = vmlaq_f32(bLR, vld1q_f32(&m_hrtfLR[bankB][t]), xL);
                bRR = vmlaq_f32(bRR, vld1q_f32(&m_hrtfRR[bankB][t]), xR);
                bRL = vmlaq_f32(bRL, vld1q_f32(&m_hrtfRL[bankB][t]), xR);
            }
            const float bL = hsum(bLL) + hsum(bRL);
            const float bR = hsum(bRR) + hsum(bLR);
            outL = outL * (1.0f - xfadeW) + bL * xfadeW;
            outR = outR * (1.0f - xfadeW) + bR * xfadeW;
        }

        // FIX DAC USB-C: blend wet (HRTF) + dry (original) — preserva la señal
        // estéreo pura cuando wet < 1, evitando artefactos al cambiar ruta de audio.
        const float dryL = m_histL[HRTF_TAPS - 1 + i];
        const float dryR = m_histR[HRTF_TAPS - 1 + i];
        buffer->left[i]  = wet * outL + dry * dryL;
        buffer->right[i] = wet * outR + dry * dryR;
    }
#else
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        const float wet = m_wetEnv.nextSample();
        const float dry = 1.0f - wet;
        float outL = 0.f, outR = 0.f;
        for (size_t t = 0; t < HRTF_TAPS; ++t) {
            const float xL = m_histL[i + t];
            const float xR = m_histR[i + t];
            outL += xL * m_hrtfLL[bankA][t] + xR * m_hrtfRL[bankA][t];
            outR += xR * m_hrtfRR[bankA][t] + xL * m_hrtfLR[bankA][t];
        }
        if (doXfade) {
            float bL = 0.f, bR = 0.f;
            for (size_t t = 0; t < HRTF_TAPS; ++t) {
                const float xL = m_histL[i + t];
                const float xR = m_histR[i + t];
                bL += xL * m_hrtfLL[bankB][t] + xR * m_hrtfRL[bankB][t];
                bR += xR * m_hrtfRR[bankB][t] + xL * m_hrtfLR[bankB][t];
            }
            outL = outL * (1.f - xfadeW) + bL * xfadeW;
            outR = outR * (1.f - xfadeW) + bR * xfadeW;
        }
        // FIX DAC USB-C: mismo blend wet/dry que el path NEON
        const float dryL = m_histL[HRTF_TAPS - 1 + i];
        const float dryR = m_histR[HRTF_TAPS - 1 + i];
        buffer->left[i]  = wet * outL + dry * dryL;
        buffer->right[i] = wet * outR + dry * dryR;
    }
#endif

    // ── Shift de historial: mover las últimas HRTF_TAPS-1 muestras al inicio
    // FIX (bug de offset): el shift anterior movía HRTF_TAPS-1 elementos desde
    // el índice BLOCK_SIZE, que no coincidía con el inicio del overlap correcto.
    // El overlap-save correcto: copiar los últimos (HRTF_TAPS-1) samples del
    // bloque actual al inicio del buffer de historia.
    std::memmove(m_histL, m_histL + BLOCK_SIZE, (HRTF_TAPS - 1) * sizeof(float));
    std::memmove(m_histR, m_histR + BLOCK_SIZE, (HRTF_TAPS - 1) * sizeof(float));
}

void HrtfManager::flushHistory() noexcept {
    // FIX DAC USB-C: limpia el historial overlap-save. Llamar inmediatamente
    // después de setWetDry(0.f) cuando el sistema rotea al DAC USB-C.
    // Sin esto, las muestras cacheadas en m_histL/m_histR producen ruido
    // de correlación (sonido de "canal de TV sin señal") en el primer
    // bloque procesado por el nuevo dispositivo de salida.
    std::memset(m_histL, 0, sizeof(m_histL));
    std::memset(m_histR, 0, sizeof(m_histR));
}

bool HrtfManager::loadFromDataset(const char* path) {
    if (!m_loader.load(path)) return false;
    m_datasetLoaded = true;
    loadFromDatasetAtAzimuth(0.f, 0);
    loadFromDatasetAtAzimuth(0.f, 1);
    return true;
}

void HrtfManager::loadFromDatasetAtAzimuth(float azimuthDeg, int bank) {
    if (!m_datasetLoaded || m_loader.size() == 0) return;
    const size_t n = m_loader.size();
    float  bestDiff = 1e9f;
    size_t bestIdx  = 0;
    for (size_t i = 0; i < n; ++i) {
        const float diff = std::fabs(m_loader.entry(i).azimuthDeg - azimuthDeg);
        if (diff < bestDiff) { bestDiff = diff; bestIdx = i; }
    }
    const auto& e = m_loader.entry(bestIdx);
    const size_t cL = std::min(static_cast<size_t>(HRTF_TAPS), e.left.size());
    const size_t cR = std::min(static_cast<size_t>(HRTF_TAPS), e.right.size());

    std::memset(m_hrtfLL[bank], 0, HRTF_TAPS * sizeof(float));
    std::memset(m_hrtfRR[bank], 0, HRTF_TAPS * sizeof(float));
    std::memset(m_hrtfLR[bank], 0, HRTF_TAPS * sizeof(float));
    std::memset(m_hrtfRL[bank], 0, HRTF_TAPS * sizeof(float));

    constexpr size_t kSofaLen = static_cast<size_t>(ivanna::master::kMasterHrirLen);
    for (size_t k = 0; k < cL; ++k) {
        float val = e.left[k];
        if (k < kSofaLen) {
            for (int c = 0; c < ivanna::master::kMasterSafK; ++c) {
                val += ivanna::master::kMasterSafGoldenQ[c] * ivanna::master::kMasterSofaPcaV[c][k];
            }
        }
        m_hrtfLL[bank][HRTF_TAPS - 1 - k] = val;
    }
    for (size_t k = 0; k < cR; ++k) {
        float val = e.right[k];
        if (k < kSofaLen) {
            for (int c = 0; c < ivanna::master::kMasterSafK; ++c) {
                val += ivanna::master::kMasterSafGoldenQ[c] * ivanna::master::kMasterSofaPcaV[c][kSofaLen + k];
            }
        }
        m_hrtfRR[bank][HRTF_TAPS - 1 - k] = val;
    }
}

} // namespace Ivanna
