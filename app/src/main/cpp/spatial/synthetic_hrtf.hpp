// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#pragma once

#include <cmath>
#include <vector>
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

        dsAz_.resize(numDirs);
        dsL_.resize(numDirs);
        dsR_.resize(numDirs);
        for (int i = 0; i < numDirs; ++i) {
            int s = idx[i];
            dsAz_[i] = az[s];
            dsL_[i].assign(irL + (size_t)s * irLen, irL + (size_t)(s + 1) * irLen);
            dsR_[i].assign(irR + (size_t)s * irLen, irR + (size_t)(s + 1) * irLen);
        }
        dsIrLen_ = irLen;
        hasDataset_ = true;
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

    bool hasCustomDataset() const { return hasDataset_; }

    HRIRPair generate(float azimuthDeg, float aggressiveness) const {
        if (!std::isfinite(azimuthDeg)) azimuthDeg = 0.f;
        if (!std::isfinite(aggressiveness)) aggressiveness = 0.5f;
        aggressiveness = std::clamp(aggressiveness, 0.f, 1.f);

        // Prioridad: dataset medido > modelo sintético
        if (hasDataset_ && !dsAz_.empty()) {
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
        return out;
    }

private:
    HRIRPair generateFromDataset(float azimuthDeg) const {
        HRIRPair out;
        out.L.assign(irLen_, 0.f);
        out.R.assign(irLen_, 0.f);
        const int n = (int)dsAz_.size();
        if (n == 0) return out;
        if (n == 1) {
            for (int k = 0; k < irLen_ && k < dsIrLen_; ++k) {
                out.L[k] = dsL_[0][k];
                out.R[k] = dsR_[0][k];
            }
            return out;
        }
        int lo = 0;
        while (lo < n - 1 && dsAz_[lo + 1] < azimuthDeg) ++lo;
        int hi = std::min(lo + 1, n - 1);
        float a0 = dsAz_[lo], a1 = dsAz_[hi];
        float t = (a1 > a0) ? std::clamp((azimuthDeg - a0) / (a1 - a0), 0.f, 1.f) : 0.f;
        for (int k = 0; k < irLen_; ++k) {
            float l0 = (k < dsIrLen_) ? dsL_[lo][k] : 0.f;
            float l1 = (k < dsIrLen_) ? dsL_[hi][k] : 0.f;
            float r0 = (k < dsIrLen_) ? dsR_[lo][k] : 0.f;
            float r1 = (k < dsIrLen_) ? dsR_[hi][k] : 0.f;
            out.L[k] = (1.f - t) * l0 + t * l1;
            out.R[k] = (1.f - t) * r0 + t * r1;
        }
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

    float sr_    = 96000.f;
    int   irLen_ = 128;

    // Dataset personalizado
    bool  hasDataset_ = false;
    int   dsIrLen_ = 0;
    std::vector<float> dsAz_;
    std::vector<std::vector<float>> dsL_, dsR_;
};

} // namespace ivanna
