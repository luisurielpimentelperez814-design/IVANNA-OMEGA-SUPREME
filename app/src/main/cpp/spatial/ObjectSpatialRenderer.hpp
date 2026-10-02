#pragma once

#include "StereoObjectDecomposer.hpp"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <algorithm>

namespace ivanna::spatial {

// ============================================================================
// ObjectSpatialRenderer — Renderizador Binaural Esférico 3D (M8)
//   - ITD esférico exacto de Woodworth (1938):
//       theta = atan2(|x|, max(y, 0)),  |ITD(theta)| = (a/c) * (|theta| + sin|theta|) * itdScale
//     con radio craneal a = 0.0875 m y c = 343.0 m/s (~31.48 muestras @ 48 kHz, theta = pi/2).
//   - Interpolación fraccionaria lineal libre de clicks sobre línea de retardo circular.
//   - Difracción craneal de Brown & Duda (1998): filtro de sombra de cabeza de 1er orden
//     a fc = 1500 Hz que atenúa altas frecuencias en el oído contralateral (> 4 dB @ 4 kHz, pi/2).
//   - Clave espectral de elevación vertical Z (pinna notch/boost > 3.5 kHz) y ley 1/r 3D.
// ============================================================================
class ObjectSpatialRenderer {
public:
    static constexpr int   kItdBuf    = 128;   // >= 65 muestras hasta 96 kHz
    static constexpr float kHeadRadM  = 0.0875f;
    static constexpr float kSoundSpdM = 343.0f;

    ObjectSpatialRenderer() noexcept {
        prepare(48000.0f);
    }

    void prepare(float sr) noexcept {
        fs_  = (sr > 8000.0f) ? sr : 48000.0f;
        aSh_ = 1.0f - std::exp(-6.283185307179586f * 1500.0f / fs_);
        reset();
    }

    void reset() noexcept {
        smoothGain_ = 1.0f;
        pinnaLpState_.fill(0.0f);
        std::memset(dlL_, 0, sizeof(dlL_));
        std::memset(dlR_, 0, sizeof(dlR_));
        std::memset(wIdx_, 0, sizeof(wIdx_));
        std::memset(lpL_, 0, sizeof(lpL_));
        std::memset(lpR_, 0, sizeof(lpR_));
    }

    void setStageElevationOffset(float elevMeters) noexcept {
        if (std::isfinite(elevMeters)) {
            stageElevOffset_ = std::clamp(elevMeters, -0.5f, 1.0f);
        }
    }

    void setEarlyReflectionGains(const std::array<float, 4>& gains) noexcept {
        for (size_t i = 0; i < 4; ++i) {
            erGains_[i] = std::isfinite(gains[i]) ? std::clamp(gains[i], 0.0f, 1.0f) : 0.15f;
        }
    }

    void setEstimatedRoomT60(float rt60Sec) noexcept {
        if (std::isfinite(rt60Sec)) {
            estimatedRoomT60_ = std::clamp(rt60Sec, 0.12f, 2.50f);
        }
    }

    float estimatedRoomT60() const noexcept {
        return estimatedRoomT60_;
    }

    // Cálculo analítico de Woodworth para ángulo azimutal theta (rad)
    static float woodworthItdSamplesFromAngle(float thetaRad,
                                              float fs = 48000.0f,
                                              float itdScale = 1.0f) noexcept {
        if (itdScale <= 0.0f) return 0.0f;
        const float th = std::clamp(std::fabs(thetaRad), 0.0f, 1.57079632679f);
        const float scale = std::clamp(itdScale, 0.0f, 1.5f);
        const float itdSec = (kHeadRadM / kSoundSpdM) * (th + std::sin(th)) * scale;
        const float safeFs = (fs > 8000.0f) ? fs : 48000.0f;
        return std::clamp(itdSec * safeFs, 0.0f, static_cast<float>(kItdBuf - 4));
    }

    // Cálculo de Woodworth a partir de coordenadas cartesianas (x, y)
    static float computeWoodworthItdSamples(float x,
                                            float y,
                                            float fs = 48000.0f,
                                            float itdScale = 1.0f) noexcept {
        const float absX = std::fabs(x);
        const float posY = std::max(y, 0.0f);
        const float th = (absX < 1.0e-7f && posY < 1.0e-7f)
                       ? 0.0f
                       : std::atan2(absX, posY);
        return woodworthItdSamplesFromAngle(th, fs, itdScale);
    }

    // Calcula la ganancia ILD (sin sombra de cabeza) para inspección / prueba
    static void computeIldGains(float x, float y, float& gainL, float& gainR) noexcept {
        const float absX = std::fabs(x);
        const float posY = std::max(y, 0.0f);
        const float th = (absX < 1.0e-7f && posY < 1.0e-7f) ? 0.0f : std::atan2(absX, posY);
        const float signedSin = (x >= 0.0f) ? std::sin(th) : -std::sin(th);
        const float angle = (signedSin + 1.0f) * 0.78539816339f; // [0, pi/2]
        gainL = std::cos(angle);
        gainR = std::sin(angle);
    }

    void renderObjects(
        const float* const objectStreams[4],
        const std::array<DecomposedObject, 4>& objects,
        float* outL,
        float* outR,
        size_t numSamples,
        float itdScale = 1.0f,
        float widthScale = 1.0f) noexcept
    {
        if (!outL || !outR || numSamples == 0) return;

        for (size_t i = 0; i < numSamples; ++i) {
            outL[i] = 0.0f;
            outR[i] = 0.0f;
        }

        const float clampedWidth = std::clamp(widthScale, 0.2f, 2.0f);
        const float clampedItd   = (itdScale <= 0.0f) ? 0.0f : std::clamp(itdScale, 0.5f, 1.5f);

        for (size_t k = 0; k < 4; ++k) {
            const float* src = objectStreams[k];
            if (!src) continue;

            const auto& obj = objects[k];
            const float scaledX = obj.position.x * clampedWidth;
            const float rawY    = obj.position.y;
            const float depth   = std::max(0.5f, rawY);
            // Objetos ambientales/armónicos (k >= 1) reciben el offset de elevación 3D del Atlas
            const float zOffset = (k >= 1) ? stageElevOffset_ : 0.0f;
            const float zElev   = std::clamp(obj.position.z + zOffset, -2.0f, 2.5f);

            // Ángulo azimutal theta = atan2(|x|, max(y, 0)) para Woodworth ITD e ILD
            const float absX = std::fabs(scaledX);
            const float posY = std::max(rawY, 0.0f);
            const float th   = (absX < 1.0e-7f && posY < 1.0e-7f)
                             ? 0.0f
                             : std::atan2(absX, posY);
            const float sTh  = std::sin(th);

            // Atenuación física por distancia 3D (x, y, z)
            const float dist3D    = std::sqrt(scaledX * scaledX + depth * depth + zElev * zElev);
            const float distAtten = 1.0f / std::max(0.75f, 0.65f + 0.35f * dist3D);

            // Panorámica de potencia constante basada en sin(theta) con signo
            const float signedSin = (scaledX >= 0.0f) ? sTh : -sTh;
            const float panAngle  = (signedSin + 1.0f) * 0.78539816339f; // [0, pi/2]
            const float gainL     = std::cos(panAngle) * obj.gain * distAtten;
            const float gainR     = std::sin(panAngle) * obj.gain * distAtten;

            // Clave espectral de pinna para elevación Z (> 3.5 kHz)
            const float pinnaElevGain = std::clamp(1.0f + 0.18f * zElev, 0.75f, 1.30f);

            // Woodworth ITD fraccionario en muestras: (a/c)*(theta + sin(theta)) * itdScale * fs
            const float itdSamples = woodworthItdSamplesFromAngle(th, fs_, clampedItd);
            const float dL = (scaledX >  1.0e-5f) ? itdSamples : 0.0f;
            const float dR = (scaledX < -1.0e-5f) ? itdSamples : 0.0f;

            // Sombra de cabeza de Brown-Duda (> 1.5 kHz en el oído contralateral)
            const float gSh = 1.0f - 0.44f * sTh;

            for (size_t i = 0; i < numSamples; ++i) {
                smoothGain_ = 0.995f * smoothGain_ + 0.005f * 1.0f;
                const float rawSample = std::isfinite(src[i]) ? src[i] : 0.0f;

                pinnaLpState_[k] += 0.32f * (rawSample - pinnaLpState_[k]);
                const float highBand = rawSample - pinnaLpState_[k];
                const float s = (pinnaLpState_[k] + highBand * pinnaElevGain) * smoothGain_;

                const int w = wIdx_[k];
                dlL_[k][w] = s * gainL;
                dlR_[k][w] = s * gainR;

                auto readFrac = [&](const float (&buf)[kItdBuf], float d) noexcept -> float {
                    const int   di = static_cast<int>(d);
                    const float fr = d - static_cast<float>(di);
                    const float s0 = buf[(w - di)     & (kItdBuf - 1)];
                    const float s1 = buf[(w - di - 1) & (kItdBuf - 1)];
                    return s0 + fr * (s1 - s0);
                };

                float l = readFrac(dlL_[k], dL);
                float r = readFrac(dlR_[k], dR);

                lpL_[k] += aSh_ * (l - lpL_[k]);
                lpR_[k] += aSh_ * (r - lpR_[k]);

                if (scaledX > 1.0e-5f) {
                    // Fuente a la derecha -> oído izquierdo es contralateral (sombra craneal HF)
                    l = lpL_[k] + gSh * (l - lpL_[k]);
                } else if (scaledX < -1.0e-5f) {
                    // Fuente a la izquierda -> oído derecho es contralateral (sombra craneal HF)
                    r = lpR_[k] + gSh * (r - lpR_[k]);
                }

                wIdx_[k] = (w + 1) & (kItdBuf - 1);
                outL[i] += l;
                outR[i] += r;
            }
        }
    }

private:
    float fs_{48000.0f};
    float aSh_{0.178f};
    float smoothGain_{1.0f};
    float stageElevOffset_{0.0f};
    float estimatedRoomT60_{0.38f};
    std::array<float, 4> erGains_{0.24f, 0.18f, 0.14f, 0.10f};
    std::array<float, 4> pinnaLpState_{0.0f, 0.0f, 0.0f, 0.0f};
    float dlL_[4][kItdBuf]{};
    float dlR_[4][kItdBuf]{};
    int   wIdx_[4]{};
    float lpL_[4]{};
    float lpR_[4]{};
};

} // namespace ivanna::spatial
