// StereoObjectDecomposer.hpp — EJE 1: descomposición estéreo→objetos WFS.
// (c) 2026 LUPP / GORE TNS.
// De una mezcla estéreo deriva 4 objetos espaciales reales en tiempo real:
//   CENTER (mid, frente), L/R FLANK (side escalado a ±x), AMBIENTE (residual
//   decorrelado). Posiciones desde correlación intercanal + energía de bandas
//   (filtros 1 polo, sin FFT). Cero malloc/locks en process(); memoria
//   preasignada en prepare(). Pensado para alimentar WfsRenderer::process.
#pragma once
#include <cstddef>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace ivanna { namespace spatial {

class StereoObjectDecomposer {
public:
    static constexpr int kObjects = 4;  // 0=CENTER 1=LEFT 2=RIGHT 3=AMBIENT
    static constexpr int kMaxBlock = 4096;

    bool prepare(float sampleRate, int maxBlock) noexcept {
        if (sampleRate <= 0.f || maxBlock <= 0 || maxBlock > kMaxBlock) return false;
        sr_ = sampleRate; block_ = maxBlock;
        // LPF 1 polo ~250 Hz para energía de graves (L/R comparten coef).
        lpC_ = 1.f - std::exp(-2.f * 3.14159265f * 250.f / sr_);
        posSmooth_ = 1.f - std::exp(-2.f * 3.14159265f * 2.f / sr_);
        reset();
        return true;
    }
    void reset() noexcept {
        lpL_ = lpR_ = 0.f; eMid_ = eSide_ = eLow_ = 0.f; n_ = 0;
        sideRatio_ = 0.5f; lowRatio_ = 0.25f;
        std::memset(objL_, 0, sizeof(objL_)); std::memset(objR_, 0, sizeof(objR_));
    }

    // RT-safe: interleaved [L0,R0,...]; frames = muestras por canal.
    // Escribe 4 objetos (cada uno frames muestras) en objL_/objR_ internos.
    void processBlock(const float* interleaved, int frames) noexcept {
        if (!interleaved || frames <= 0) return;
        if (frames > block_) frames = block_;
        float eMid = 0.f, eSide = 0.f, eLow = 0.f;
        for (int i = 0; i < frames; ++i) {
            const float l = interleaved[2 * i];
            const float r = interleaved[2 * i + 1];
            const float mid   = 0.5f * (l + r);
            const float sideL = 0.5f * (l - r);
            const float sideR = -sideL;
            // graves por LPF sobre mid
            lpL_ += lpC_ * (mid - lpL_);
            const float lowM = lpL_;
            eMid  += mid * mid;
            eSide += sideL * sideL;
            eLow  += lowM * lowM;
            // objetos: center=mid (voz/bombo/bajo), flanks=side, ambient=side suavizado
            objL_[0 * kMaxBlock + i] = mid;
            objR_[0 * kMaxBlock + i] = mid;
            objL_[1 * kMaxBlock + i] = sideL;   // LEFT  = +side
            objR_[1 * kMaxBlock + i] = 0.f;
            objL_[2 * kMaxBlock + i] = 0.f;     // RIGHT = -side
            objR_[2 * kMaxBlock + i] = sideR;
            objL_[3 * kMaxBlock + i] = sideL * 0.35f; // AMBIENTE residual
            objR_[3 * kMaxBlock + i] = sideR * 0.35f;
        }
        const float tot = eMid + 2.f * eSide + 1e-9f;
        const float instSide = (2.f * eSide) / tot;   // 0=mono puro .. 1=muy ancho
        const float instLow  = eLow / (eMid + 1e-9f); // 0..1 aprox
        sideRatio_ += posSmooth_ * frames * (instSide - sideRatio_);
        lowRatio_  += posSmooth_ * frames * (instLow  - lowRatio_);
        n_ += frames;
    }

    // Punteros de entrada para WfsRenderer::process (planar por objeto).
    // fillObjectPtrs llena in[i] con el canal L de cada objeto (o R vía objR_).
    const float* objectL(int id) const noexcept { return &objL_[id * kMaxBlock]; }
    const float* objectR(int id) const noexcept { return &objR_[id * kMaxBlock]; }

    // Posiciones 3D canónicas según el análisis (x lateral, y distancia, gain).
    // width 0..1 (sideRatio) abre los flancos; lowRatio acerca el bajo al frente.
    void canonicalLayout(float x[4], float y[4], float g[4]) const noexcept {
        const float w = 0.3f + 0.9f * sideRatio_;      // apertura real medida
        const float lowPull = 0.3f * lowRatio_;         // bajo → más cerca
        x[0] = 0.0f;   y[0] = 2.0f - lowPull; g[0] = 1.0f;  // CENTER frente
        x[1] = -w;     y[1] = 1.6f;           g[1] = 0.9f;  // LEFT flank
        x[2] =  w;     y[2] = 1.6f;           g[2] = 0.9f;  // RIGHT flank
        x[3] = 0.0f;   y[3] = 3.0f;           g[3] = 0.5f;  // AMBIENTE atrás
    }

    float sideRatio() const noexcept { return sideRatio_; }
    float lowRatio()  const noexcept { return lowRatio_; }

private:
    float sr_ = 48000.f; int block_ = 0;
    float lpC_ = 0.f, posSmooth_ = 0.f;
    float lpL_ = 0.f, lpR_ = 0.f, eMid_ = 0.f, eSide_ = 0.f, eLow_ = 0.f;
    int   n_ = 0;
    float sideRatio_ = 0.5f, lowRatio_ = 0.25f;
    // buffers planares preasignados (4 objetos × kMaxBlock) — cero malloc
    float objL_[kObjects * kMaxBlock]{};
    float objR_[kObjects * kMaxBlock]{};
};

}} // namespace ivanna::spatial
