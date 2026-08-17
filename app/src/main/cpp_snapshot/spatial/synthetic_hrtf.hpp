// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#pragma once

#include <cmath>
#include <vector>
#include <memory>
#include <atomic>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdint>

namespace ivanna {

struct HRIRPair {
    std::vector<float> L;
    std::vector<float> R;
};

class SyntheticHRTF {
public:
    void init(uint32_t sampleRate, int irLen) {
        sr_ = (float)sampleRate;
        irLen_ = irLen;
    }

    // ── Dataset HRTF personalizado ─────────────────────────────────────
    // Carga HRIRs medidos indexados por azimut. Si hay dataset, generate()
    // interpola de aquí; si no, usa el modelo sintético (fallback).
    bool loadDataset(const float* azimuthsDeg, const float* irL, const float* irR,
                     int numDirs, int irLen) {
        if (numDirs <= 0 || irLen <= 0 || !azimuthsDeg || !irL || !irR) return false;
        std::vector<int> idx(numDirs);
        for (int i = 0; i < numDirs; ++i) idx[i] = i;
        std::vector<float> az(azimuthsDeg, azimuthsDeg + numDirs);
        std::sort(idx.begin(), idx.end(),
                  [&](int a, int b) { return az[a] < az[b]; });

        auto ds = std::make_shared<SharedDataset>();
        ds->irLen = irLen;
        ds->az.resize(numDirs);
        ds->L.resize(numDirs);
        ds->R.resize(numDirs);
        for (int i = 0; i < numDirs; ++i) {
            int s = idx[i];
            ds->az[i] = az[s];
            ds->L[i].assign(irL + (size_t)s * irLen, irL + (size_t)(s + 1) * irLen);
            ds->R[i].assign(irR + (size_t)s * irLen, irR + (size_t)(s + 1) * irLen);
        }
        std::atomic_store(&dataset_, ds);
        return true;
    }

    // Formato binario "IHR1":
    //   [4B magic "IHR1"][int32 numDirs][int32 irLen][int32 sampleRate]
    //   por dirección: [float azimuthDeg][irLen floats L][irLen floats R]
    bool loadDatasetFromFile(const char* path) {
        if (!path) return false;
        FILE* f = std::fopen(path, "rb");
        if (!f) return false;
        char magic[4];
        if (std::fread(magic, 1, 4, f) != 4 || std::memcmp(magic, "IHR1", 4) != 0) {
            std::fclose(f); return false;
        }
        int32_t numDirs = 0, irLen = 0, sr = 0;
        if (std::fread(&numDirs, 4, 1, f) != 1 ||
            std::fread(&irLen, 4, 1, f) != 1 ||
            std::fread(&sr, 4, 1, f) != 1) { std::fclose(f); return false; }
        if (numDirs <= 0 || irLen <= 0 || numDirs > 1024 || irLen > 8192) {
            std::fclose(f); return false;
        }
        std::vector<float> az(numDirs);
        std::vector<float> L((size_t)numDirs * irLen);
        std::vector<float> R((size_t)numDirs * irLen);
        for (int d = 0; d < numDirs; ++d) {
            if (std::fread(&az[d], 4, 1, f) != 1) { std::fclose(f); return false; }
            if (std::fread(&L[(size_t)d * irLen], 4, irLen, f) != (size_t)irLen) { std::fclose(f); return false; }
            if (std::fread(&R[(size_t)d * irLen], 4, irLen, f) != (size_t)irLen) { std::fclose(f); return false; }
        }
        std::fclose(f);
        return loadDataset(az.data(), L.data(), R.data(), numDirs, irLen);
    }

    // ── Base PCA V (morph algebraico exacto: HRIR += V·q) ─────────────
    // Formato: [4B "PCAV"][i32 K][i32 irLen] + por componente [irLen f32 L][irLen f32 R]
    // Generado por scripts/compute_pca_basis.py desde hrtf_dataset.ihr1.
    bool loadPcaBasis(const char* path) {
        if (!path) return false;
        FILE* f = std::fopen(path, "rb");
        if (!f) return false;
        char magic[4];
        if (std::fread(magic, 1, 4, f) != 4 || std::memcmp(magic, "PCAV", 4) != 0) {
            std::fclose(f); return false;
        }
        int32_t K = 0, irLen = 0;
        if (std::fread(&K, 4, 1, f) != 1 || std::fread(&irLen, 4, 1, f) != 1) {
            std::fclose(f); return false;
        }
        if (K <= 0 || K > 64 || irLen <= 0 || irLen > 8192) { std::fclose(f); return false; }
        std::vector<float> v((size_t)K * 2 * irLen);
        if (std::fread(v.data(), 4, v.size(), f) != v.size()) { std::fclose(f); return false; }
        std::fclose(f);
        pcaV_.swap(v);
        pcaK_ = K;
        pcaIrLen_ = irLen;
        return true;
    }

    bool hasCustomDataset() const {
        return std::atomic_load(&dataset_) != nullptr;
    }

    // Dataset compartido: una sola copia en memoria para todos los
    // convolvers del renderer (HRTFConvolver::setSharedDataset la propaga).
    // PUBLICO: hrtf_convolver.hpp e ivanna_object_renderer.hpp referencian
    // SyntheticHRTF::SharedDataset directamente.
    struct SharedDataset {
        int irLen = 0;
        std::vector<float> az;
        std::vector<std::vector<float>> L, R;
    };
    void setSharedDataset(std::shared_ptr<SharedDataset> ds) noexcept {
        std::atomic_store(&dataset_, std::move(ds));
    }

    // ── SAF latent morphing ────────────────────────────────────────────
    // Aplica el vector q_t (7 componentes PCA) del optimizador Φ_SAF^∞
    // como un morph lineal sobre los HRIRs del dataset.
    //
    // Modelo: HRIR_personalizado = HRIR_base + Σ q[k] * basis_k
    //
    // Dado que no tenemos la base PCA expandida en muestras de tiempo
    // (eso requeriría cargar la matriz V de SAF_model.json que es 7×irLen),
    // usamos una aproximación práctica de alta fidelidad:
    //   - q[0] → ganancia broadband (escala global del HRIR)
    //   - q[1] → balance L/R (ITD proxy: ganancia diferencial oído contralateral)
    //   - q[2] → notch front-back 8-10 kHz (profundidad del filtro de pinna)
    //   - q[3] → elevación: refuerzo de altas frecuencias (concha)
    //   - q[4] → elevación: atenuación de medias (anti-helix)
    //   - q[5] → textura espectral fina canal L (ridges de pinna)
    //   - q[6] → textura espectral fina canal R
    //
    // Los rangos de q[k] son ±3σ según kPMax/kPMin del SaFOptimizer.
    // Esta función es llamada desde el hilo de control (no audio) tras
    // cada feedFeedback(); actualiza latentL_/latentR_ que generate()
    // lee de forma segura vía flag atómico latentDirty_.
    void setLatentParams(const float q[7]) noexcept {
        // Normalizar q a [-1,1] usando los rangos ±3σ del optimizador
        // (kPMax[k] ≈ 3*sqrt(kG0[k])). Aproximación suficiente sin cargar
        // el JSON completo en esta capa.
        static constexpr float kSigma[7] = {
            0.02888f, 0.03102f, 0.03354f,
            0.03923f, 0.05319f, 0.08036f, 0.16148f
        };
        float qn[7];
        for (int i = 0; i < 7; ++i) {
            qn[i] = (kSigma[i] > 0.f)
                ? std::clamp(q[i] / kSigma[i], -1.f, 1.f)
                : 0.f;
        }
        // Guardar para que generate() los aplique en la próxima llamada
        for (int i = 0; i < 7; ++i) latentQ_[i] = qn[i];
        latentActive_ = true;
    }

    void clearLatentParams() noexcept {
        latentActive_ = false;
        for (int i = 0; i < 7; ++i) latentQ_[i] = 0.f;
    }

    HRIRPair generate(float azimuthDeg, float aggressiveness) const {
        if (!std::isfinite(azimuthDeg)) azimuthDeg = 0.f;
        if (!std::isfinite(aggressiveness)) aggressiveness = 0.5f;
        aggressiveness = std::clamp(aggressiveness, 0.f, 1.f);

        // Prioridad: dataset medido > modelo sintético
        auto ds = std::atomic_load(&dataset_);
        if (ds && !ds->az.empty()) {
            return generateFromDataset(azimuthDeg);
        }

        const float theta = azimuthDeg * (float)M_PI / 180.f;
        const float absTheta = std::fabs(theta);

        constexpr float HEAD_R = 0.0875f;
        constexpr float SPEED  = 343.f;
        const float tau = (HEAD_R / SPEED) * (absTheta + std::sin(absTheta));
        const float itdSamples = tau * sr_;
        const int   delaySamp  = std::clamp((int)std::round(itdSamples), 0, irLen_ / 2);

        HRIRPair out;
        out.L.assign(irLen_, 0.f);
        out.R.assign(irLen_, 0.f);

        const bool sourceRight = theta >= 0.f;
        std::vector<float>& nearEar = sourceRight ? out.R : out.L;
        std::vector<float>& farEar  = sourceRight ? out.L : out.R;

        nearEar[0] = 1.f;

        const float shadowAmount = (absTheta / (float)(M_PI * 0.5)) * aggressiveness;
        const float fc = 14000.f - shadowAmount * 10500.f;
        const float rc = 1.f / (2.f * (float)M_PI * fc);
        const float dt = 1.f / sr_;
        const float alpha = dt / (rc + dt);
        const float shadowGain = 1.f - 0.3f * shadowAmount;

        float lpState = 0.f;
        for (int n = 0; n < irLen_; ++n) {
            const float impulse = (n == 0) ? 1.f : 0.f;
            lpState = lpState + alpha * (impulse - lpState);
            const int idx = n + delaySamp;
            if (idx < irLen_) farEar[idx] += lpState * shadowGain;
        }

        const float notchDepth = std::fabs(std::sin(theta)) * aggressiveness * 0.6f;
        if (notchDepth > 0.001f) {
            apply_notch_fir(nearEar, 7500.f, notchDepth);
            apply_notch_fir(farEar,  7500.f, notchDepth * 0.7f);
        }

        // Aplicar SAF latent morph si está activo
        if (latentActive_) applyLatentMorph(out);
        return out;
    }

private:
    HRIRPair generateFromDataset(float azimuthDeg) const {
        HRIRPair out;
        out.L.assign(irLen_, 0.f);
        out.R.assign(irLen_, 0.f);
        
        auto ds = std::atomic_load(&dataset_);
        if (!ds) return out;
        
        const int n = (int)ds->az.size();
        if (n == 0) return out;
        
        if (n == 1) {
            for (int k = 0; k < irLen_ && k < ds->irLen; ++k) {
                out.L[k] = ds->L[0][k];
                out.R[k] = ds->R[0][k];
            }
            return out;
        }
        
        int lo = 0;
        while (lo < n - 1 && ds->az[lo + 1] < azimuthDeg) ++lo;
        int hi = std::min(lo + 1, n - 1);
        
        float a0 = ds->az[lo], a1 = ds->az[hi];
        float t = (a1 > a0) ? std::clamp((azimuthDeg - a0) / (a1 - a0), 0.f, 1.f) : 0.f;
        
        for (int k = 0; k < irLen_; ++k) {
            float l0 = (k < ds->irLen) ? ds->L[lo][k] : 0.f;
            float l1 = (k < ds->irLen) ? ds->L[hi][k] : 0.f;
            float r0 = (k < ds->irLen) ? ds->R[lo][k] : 0.f;
            float r1 = (k < ds->irLen) ? ds->R[hi][k] : 0.f;
            out.L[k] = (1.f - t) * l0 + t * l1;
            out.R[k] = (1.f - t) * r0 + t * r1;
        }
        
        if (latentActive_) applyLatentMorph(out);
        return out;
    }

    void apply_notch_fir(std::vector<float>& buf, float freqHz, float depth) const {
        depth = std::clamp(depth, 0.f, 0.6f);
        const float w0 = 2.f * (float)M_PI * freqHz / sr_;
        const float cosw0 = std::cos(w0);
        const float a = depth * 0.2f;
        const float b = 2.f * (1.f - a * cosw0);
        const float norm = a + b + a;
        const float normFactor = (norm > 0.001f) ? 1.f / norm : 1.f;
        const float k0 = a * normFactor;
        const float k1 = b * normFactor;
        const float k2 = a * normFactor;
        std::vector<float> tmp(buf.size(), 0.f);
        for (size_t n = 0; n < buf.size(); ++n) {
            float acc = k1 * buf[n];
            if (n >= 1) acc += k0 * buf[n - 1];
            if (n + 1 < buf.size()) acc += k2 * buf[n + 1];
            tmp[n] = acc;
        }
        buf.swap(tmp);
    }

    // ── applyLatentMorph: SAF q_t → HRIR modulation ───────────────────
    // Aplica los 7 coeficientes latentes normalizados a [-1,1] sobre el
    // par de HRIRs ya generado. Cada componente afecta un rasgo distinto:
    void applyLatentMorph(HRIRPair& p) const noexcept {
        const int N = (int)p.L.size();
        if (N == 0) return;
        const float* qn = latentQ_;

        // Ruta EXACTA: HRIR(az) += V·q en muestras de tiempo (base PCA real
        // del dataset). Cuando V no está cargada se cae a la aproximación
        // por bandas espectrales de más abajo (comportamiento previo).
        if (pcaK_ > 0 && pcaIrLen_ > 0 &&
            (int)pcaV_.size() == pcaK_ * 2 * pcaIrLen_) {
            const int M2 = 2 * pcaIrLen_;
            const int lim = N < pcaIrLen_ ? N : pcaIrLen_;
            for (int n = 0; n < lim; ++n) {
                float dL = 0.f, dR = 0.f;
                for (int k = 0; k < pcaK_; ++k) {
                    const float q = qn[k];
                    if (q == 0.f) continue;
                    dL += q * pcaV_[(size_t)k * M2 + n];
                    dR += q * pcaV_[(size_t)k * M2 + pcaIrLen_ + n];
                }
                p.L[n] += dL;
                p.R[n] += dR;
            }
            return;
        }

        // q[0]: ganancia broadband ±20% — PC0 = forma espectral global
        const float gain = 1.f + 0.2f * qn[0];
        for (int k = 0; k < N; ++k) {
            p.L[k] *= gain;
            p.R[k] *= gain;
        }

        // q[1]: balance L/R ±15% — PC1 = ITD/ILD lateral
        const float balScale = 0.15f * qn[1];
        for (int k = 0; k < N; ++k) {
            p.L[k] *= (1.f - balScale);
            p.R[k] *= (1.f + balScale);
        }

        // q[2]: notch pinna front-back en 9 kHz, profundidad ±0.4 — PC2
        if (std::fabs(qn[2]) > 0.01f) {
            apply_notch_fir(p.L, 9000.f,  0.4f * qn[2]);
            apply_notch_fir(p.R, 9000.f,  0.4f * qn[2]);
        }

        // q[3]: refuerzo de concha (elevación), shelving HF ±0.3 — PC3
        // Implementado como complemento de un LP muy suave (shelving simple)
        {
            const float alpha3 = std::clamp(0.3f * std::fabs(qn[3]), 0.f, 0.3f);
            const float sign3  = (qn[3] >= 0.f) ? 1.f : -1.f;
            // high-shelf: y[n] = x[n] + sign3*alpha3*(x[n] - LP(x[n]))
            float lpL = 0.f, lpR = 0.f;
            const float lpA = 0.85f; // fc ≈ sr*(1-0.85)/(2π)
            for (int k = 0; k < N; ++k) {
                lpL = lpA * lpL + (1.f - lpA) * p.L[k];
                lpR = lpA * lpR + (1.f - lpA) * p.R[k];
                p.L[k] += sign3 * alpha3 * (p.L[k] - lpL);
                p.R[k] += sign3 * alpha3 * (p.R[k] - lpR);
            }
        }

        // q[4]: atenuación de medias 2-4 kHz ±0.25 — PC4 = anti-helix
        if (std::fabs(qn[4]) > 0.01f) {
            apply_notch_fir(p.L, 3000.f, 0.25f * qn[4]);
            apply_notch_fir(p.R, 3000.f, 0.25f * qn[4]);
        }

        // q[5]: textura espectral canal L (ridges pinna) ±0.15 — PC5
        if (std::fabs(qn[5]) > 0.01f) {
            apply_notch_fir(p.L, 12000.f, 0.15f * qn[5]);
        }

        // q[6]: textura espectral canal R ±0.15 — PC6
        if (std::fabs(qn[6]) > 0.01f) {
            apply_notch_fir(p.R, 12000.f, 0.15f * qn[6]);
        }
    }

    float sr_    = 96000.f;
    int   irLen_ = 128;

    // Dataset personalizado (struct declarado en la zona pública)
    std::shared_ptr<SharedDataset> dataset_;
    // Base PCA del dataset (morph exacto)
    std::vector<float> pcaV_;
    int pcaK_ = 0;
    int pcaIrLen_ = 0;


    // SAF latent state — actualizado por setLatentParams() desde el hilo de control
    bool  latentActive_ = false;
    float latentQ_[7]   = {};  // q normalizado a [-1,1]
};

} // namespace ivanna
