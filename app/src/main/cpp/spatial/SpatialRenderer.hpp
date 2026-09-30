#pragma once

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <algorithm>
#include "HRTFInterpolator.hpp"
#include "RoomSimulator.hpp"

namespace Ivanna {

struct Vector3D {
    float x; // -1.0 to +1.0 (Left/Right)
    float y; // -1.0 to +1.0 (Front/Back)
    float z; // -1.0 to +1.0 (Down/Up)
};

struct Quaternion {
    float w{1.0f}, x{0.0f}, y{0.0f}, z{0.0f};
};

/**
 * BinauralRenderer — Renderizador 3D 6DoF con rotación cuaterniónica e
 * interpolación HRTF KEMAR de 128 taps en tiempo real (Zero-Allocation).
 */
class BinauralRenderer {
public:
    static constexpr size_t HRTF_TAPS = 128;

    BinauralRenderer() noexcept {
        std::memset(m_hrtfLeft, 0, sizeof(m_hrtfLeft));
        std::memset(m_hrtfRight, 0, sizeof(m_hrtfRight));
        std::memset(m_history, 0, sizeof(m_history));
        m_hrtfLeft[0] = 1.0f;
        m_hrtfRight[0] = 1.0f;
    }

    void setOrientation(const Quaternion& quat) noexcept {
        const float normSq = quat.w * quat.w + quat.x * quat.x + quat.y * quat.y + quat.z * quat.z;
        if (normSq > 1.0e-8f) {
            const float inv = 1.0f / std::sqrt(normSq);
            m_orientation = {quat.w * inv, quat.x * inv, quat.y * inv, quat.z * inv};
        } else {
            m_orientation = {1.0f, 0.0f, 0.0f, 0.0f};
        }
        m_filtersDirty = true;
    }

    void processBinaural(const float* inMono, float* outLeft, float* outRight,
                         size_t numFrames, Vector3D position) noexcept {
        if (!inMono || !outLeft || !outRight || numFrames == 0) return;

        // 1. Rotar vector de fuente por el conjugado del cuaternión de orientación (head-tracking 6DoF)
        const float qw = m_orientation.w;
        const float qx = -m_orientation.x;
        const float qy = -m_orientation.y;
        const float qz = -m_orientation.z;

        const float tx = 2.0f * (qy * position.z - qz * position.y);
        const float ty = 2.0f * (qz * position.x - qx * position.z);
        const float tz = 2.0f * (qx * position.y - qy * position.x);

        const float rx = position.x + qw * tx + (qy * tz - qz * ty);
        const float ry = position.y + qw * ty + (qz * tx - qx * tz);
        const float rz = position.z + qw * tz + (qx * ty - qy * tx);

        // 2. Conversión a coordenadas esféricas (Azimut [-180..180], Elevación [-45..45], Distancia)
        const float distSq = rx * rx + ry * ry + rz * rz;
        const float dist = std::sqrt(std::max(0.25f, distSq));
        const float distGain = 1.0f / std::sqrt(dist);

        constexpr float kRadToDeg = 57.2957795f;
        const float azDeg = std::atan2(rx, std::max(1.0e-4f, ry)) * kRadToDeg;
        const float horiz = std::sqrt(rx * rx + ry * ry);
        const float elDeg = std::clamp(std::atan2(rz, std::max(1.0e-4f, horiz)) * kRadToDeg, -45.0f, 45.0f);

        if (m_filtersDirty ||
            std::fabs(azDeg - m_lastAzDeg) > 0.25f ||
            std::fabs(elDeg - m_lastElDeg) > 0.25f) {
            m_interpolator.getInterpolatedHRTF(azDeg, elDeg, m_hrtfLeft, m_hrtfRight);
            // Garantizar piso de energía si el par HRTF es nulo
            float energy = 0.0f;
            for (size_t k = 0; k < HRTF_TAPS; ++k) {
                energy += m_hrtfLeft[k] * m_hrtfLeft[k] + m_hrtfRight[k] * m_hrtfRight[k];
            }
            if (energy < 1.0e-8f) {
                const float pan = std::clamp(rx, -1.0f, 1.0f);
                m_hrtfLeft[0]  = std::sqrt(0.5f * (1.0f - pan));
                m_hrtfRight[0] = std::sqrt(0.5f * (1.0f + pan));
            }
            m_lastAzDeg = azDeg;
            m_lastElDeg = elDeg;
            m_filtersDirty = false;
        }

        // 3. Convolución FIR circular de 128 taps libre de asignaciones dinámicas
        for (size_t i = 0; i < numFrames; ++i) {
            m_history[m_histIdx] = inMono[i] * distGain;

            float accL = 0.0f;
            float accR = 0.0f;
            size_t hIdx = m_histIdx;
            for (size_t tap = 0; tap < HRTF_TAPS; ++tap) {
                const float s = m_history[hIdx];
                accL += s * m_hrtfLeft[tap];
                accR += s * m_hrtfRight[tap];
                hIdx = (hIdx == 0) ? (HRTF_TAPS - 1) : (hIdx - 1);
            }

            m_histIdx = (m_histIdx + 1) & (HRTF_TAPS - 1);
            outLeft[i]  = accL;
            outRight[i] = accR;
        }
    }

private:
    HRTFInterpolator m_interpolator{};
    float m_hrtfLeft[HRTF_TAPS];
    float m_hrtfRight[HRTF_TAPS];
    float m_history[HRTF_TAPS];
    size_t m_histIdx{0};
    Quaternion m_orientation{};
    float m_lastAzDeg{-999.0f};
    float m_lastElDeg{-999.0f};
    bool m_filtersDirty{true};
};

} // namespace Ivanna
