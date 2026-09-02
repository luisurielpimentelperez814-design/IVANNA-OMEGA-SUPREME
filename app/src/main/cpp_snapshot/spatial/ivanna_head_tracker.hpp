// ivanna_head_tracker.hpp
// ============================================================================
// IVANNA — Head Tracking 6DoF para Audio Holográfico
// ============================================================================
// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// [MAJESTY-6DoF-1.0] Rastreo de cabeza en tiempo real usando IMU del
// dispositivo Android (Sensor.TYPE_ROTATION_VECTOR). Convierte la orientación
// del dispositivo en una matriz de rotación 3D que se aplica a los coeficientes
// HRTF en cada frame de audio.
//
// Esto permite que el sonido "se quede fijo en el espacio" cuando el usuario
// gira la cabeza — la experiencia holográfica que Dolby Atmos NO ofrece en
// dispositivos genéricos (solo en auriculares propietarios como AirPods Pro).
//
// Latencia objetivo: <5ms desde sample IMU hasta aplicación HRTF.
// ============================================================================
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace ivanna::spatial {

struct alignas(16) Quaternion {
    float x = 0.f, y = 0.f, z = 0.f, w = 1.f;

    inline void fromRotationVector(const float rv[4]) noexcept {
        // rv[0..2] = x,y,z; rv[3] = scalar (cos(theta/2))
        x = rv[0]; y = rv[1]; z = rv[2]; w = rv[3];
        normalize();
    }

    inline void normalize() noexcept {
        float len = std::sqrt(x*x + y*y + z*z + w*w);
        if (len > 1e-6f) { x /= len; y /= len; z /= len; w /= len; }
    }

    // Convierte quaternion a matriz de rotación 3x3 (column-major)
    inline void toRotationMatrix(float out[9]) const noexcept {
        const float xx = x*x, yy = y*y, zz = z*z;
        const float xy = x*y, xz = x*z, yz = y*z;
        const float wx = w*x, wy = w*y, wz = w*z;

        out[0] = 1.f - 2.f*(yy + zz);  out[3] = 2.f*(xy - wz);       out[6] = 2.f*(xz + wy);
        out[1] = 2.f*(xy + wz);        out[4] = 1.f - 2.f*(xx + zz); out[7] = 2.f*(yz - wx);
        out[2] = 2.f*(xz - wy);        out[5] = 2.f*(yz + wx);       out[8] = 1.f - 2.f*(xx + yy);
    }

    // Yaw (rotación alrededor del eje Z, "arriba") extraído del quaternion,
    // en grados. Convención Z-up consistente con kVirtualSpeakers (el anillo
    // ecuatorial vive en el plano X-Y, los polos son ±Z).
    inline float yawDegrees() const noexcept {
        const float sinYawCosPitch = 2.f * (w*z + x*y);
        const float cosYawCosPitch = 1.f - 2.f * (y*y + z*z);
        return std::atan2(sinYawCosPitch, cosYawCosPitch) * 180.f / (float)M_PI;
    }

    // Interpolación esférica (SLERP) para suavizar entre poses
    static Quaternion slerp(const Quaternion& a, const Quaternion& b, float t) noexcept {
        float dot = a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
        Quaternion b2 = b;
        if (dot < 0.f) { dot = -dot; b2.x = -b2.x; b2.y = -b2.y; b2.z = -b2.z; b2.w = -b2.w; }

        if (dot > 0.9995f) {
            Quaternion r;
            r.x = a.x + t*(b2.x - a.x);
            r.y = a.y + t*(b2.y - a.y);
            r.z = a.z + t*(b2.z - a.z);
            r.w = a.w + t*(b2.w - a.w);
            r.normalize();
            return r;
        }

        float theta0 = std::acos(dot);
        float theta = theta0 * t;
        float sinTheta = std::sin(theta);
        float sinTheta0 = std::sin(theta0);
        float s0 = std::cos(theta) - dot * sinTheta / sinTheta0;
        float s1 = sinTheta / sinTheta0;

        Quaternion r;
        r.x = a.x*s0 + b2.x*s1;
        r.y = a.y*s0 + b2.y*s1;
        r.z = a.z*s0 + b2.z*s1;
        r.w = a.w*s0 + b2.w*s1;
        return r;
    }
};

struct alignas(16) HeadPose {
    Quaternion orientation;
    float timestampMs = 0.f;
    float confidence = 1.f;  // 0-1, basado en calidad del sensor
};

class HeadTracker {
public:
    // Llamado desde hilo de sensor (Java/Kotlin) a ~100Hz
    void update(const float rotationVector[4], float timestampMs) noexcept;

    // Llamado desde hilo de audio a ~96000Hz — interpolación suave
    HeadPose getPoseForAudioFrame(float audioFrameTimeMs) const noexcept;

    // Reset cuando el usuario se quita los auriculares
    void reset() noexcept;

private:
    std::atomic<HeadPose> currentPose_{};
    HeadPose previousPose_{};
    float lastTimestampMs_ = 0.f;

    // Buffer circular de poses para interpolación de alta calidad
    static constexpr int kPoseHistorySize = 8;
    std::array<HeadPose, kPoseHistorySize> poseHistory_{};
    std::atomic<int> historyWriteIdx_{0};
};

} // namespace ivanna::spatial
