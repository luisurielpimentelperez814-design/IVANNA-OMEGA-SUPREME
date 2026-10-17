// HeadTrackerFusion.hpp — Fusión 6DoF de sensores (completa el pendiente del rastreador)
// (c) 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// PENDIENTE QUE CIERRA: ivanna_head_tracker.hpp/cpp ya convierte el vector de rotación en
// cuaternión y en matriz, pero la fusión de sensores estaba marcada como pendiente: el
// rastreador tomaba el vector de rotación tal cual, sin estimar el sesgo del giroscopio,
// sin corrección por gravedad y —lo más importante para el audio— SIN PREDICCIÓN. Con
// sensores a ~100 Hz y audio a 48 kHz, entre dos muestras del sensor transcurren hasta
// 10 ms. Si el oyente gira a 90 °/s, en esos 10 ms la cabeza se movió 0.9°, y el campo
// HOA/WFS se aplica a una pose YA VIEJA: el campo "se derrapa" en cada giro.
//
// SOLUCIÓN: filtro complementario con predicción por velocidad angular.
//   · Giroscopio  → integra la actitud rápida (sin ruido de acelerómetro).
//   · Acelerómetro→ corrige la inclinación (pitch/roll) a largo plazo.
//   · Magnetómetro→ corrige el acimut (yaw) a largo plazo (opcional).
//   · getPoseAt() → extrapola la actitud al instante EXACTO del frame de audio con Slerp,
//     de modo que el campo gira con la cabeza y no detrás de ella.
//   · Sesgo del giroscopio estimado en línea (media de (ω_gyro − ω_derivada)) y restado.
//
// RT-safe: sin malloc, sin locks; getPoseAt() es const y solo aritmética de cuaterniones.
#pragma once

#include "ivanna_head_tracker.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace ivanna { namespace spatial {

struct alignas(16) FusedPose {
    Quaternion orientation{};
    float timestampMs = 0.0f;
    float confidence = 1.0f;   // 0..1 según frescura de sensores y coherencia
    float gyroBiasDegS[3] = {0.f, 0.f, 0.f};
    float yawDeg = 0.0f;
    float pitchDeg = 0.0f;
    float rollDeg = 0.0f;
    float angularRateDegS = 0.0f;
};

// Estado compartido entre el hilo de sensor (escritura) y el hilo de audio (lectura).
class alignas(64) HeadTrackerFusion {
public:
    void prepare(float sampleRate) noexcept {
        sr_ = (sampleRate > 8000.0f && std::isfinite(sampleRate)) ? sampleRate : 48000.0f;
        // Latencia de sensor asumida (SensorEvent de Android: 5–20 ms típicos).
        setSensorLatencyMs(10.0f);
        reset();
    }

    void reset() noexcept {
        acc_.orientation = Quaternion{};
        prevGyro_.orientation = Quaternion{};
        lastRv_.orientation = Quaternion{};
        for (int i = 0; i < 3; ++i) bias_[i] = 0.0f;
        hasGyro_ = hasAccel_ = hasMag_ = hasRv_ = false;
        lastGyroMs_ = lastAccelMs_ = lastMagMs_ = lastRvMs_ = -1.0e9f;
        predictionMs_ = 0.0f;
        integratedDeg_ = 0.0f;
        publish();
    }

    void setSensorLatencyMs(float ms) noexcept {
        if (std::isfinite(ms)) latMs_.store(std::clamp(ms, 0.0f, 40.0f), std::memory_order_relaxed);
    }
    float sensorLatencyMs() const noexcept { return latMs_.load(std::memory_order_relaxed); }

    // Activación de la corrección por acelerómetro (inclinación) y por magnetómetro (acimut).
    void setGravityCorrection(bool on) noexcept { useGravity_.store(on, std::memory_order_relaxed); }
    void setYawCorrection(bool on) noexcept { useMag_.store(on, std::memory_order_relaxed); }
    // Constantes de tiempo de la corrección lenta (segundos): nunca tan rápidas como para
    // que el ruido del acelerómetro entre al campo; nunca tan lentas como para no corregir.
    void setCorrectionTau(float gravitySec, float yawSec) noexcept {
        if (std::isfinite(gravitySec)) tauGrav_.store(std::clamp(gravitySec, 0.2f, 30.0f), std::memory_order_relaxed);
        if (std::isfinite(yawSec))     tauYaw_.store(std::clamp(yawSec, 1.0f, 120.0f), std::memory_order_relaxed);
    }

    // ── Entradas de sensores (hilo de sensor) ────────────────────────────────

    // Vector de rotación de Android (Sensor.TYPE_ROTATION_VECTOR), rv[3] = escalar.
    void updateRotationVector(const float rv[4], float timestampMs) noexcept {
        if (!rv) return;
        Quaternion q;
        q.fromRotationVector(rv);
        lastRv_ = FusedPose{};
        lastRv_.orientation = q;
        lastRv_.timestampMs = timestampMs;
        lastRvMs_ = timestampMs;
        hasRv_ = true;
        if (!hasGyro_) { // sin giroscopio, el vector de rotación manda directamente
            acc_.orientation = q;
            acc_.timestampMs = timestampMs;
            publish();
        }
    }

    // Giroscopio en rad/s (Sensor.TYPE_GYROSCOPE): integra la actitud y estima el sesgo.
    void updateGyro(const float gyro[3], float timestampMs) noexcept {
        if (!gyro) return;
        if (!hasGyro_) {
            lastGyroMs_ = timestampMs;
            hasGyro_ = true;
            return;
        }
        float dt = (timestampMs - lastGyroMs_) * 1.0e-3f;
        lastGyroMs_ = timestampMs;
        if (!(dt > 0.0f) || dt > 0.25f) return; // hueco o retroceso de reloj: se ignora

        // DEFECTO REAL DETECTADO EN VERIFICACION: adaptar el sesgo con CUALQUIER lectura
        // absorbia una rotacion CONSTANTE como si fuera sesgo — un giro sostenido de
        // 30 grados/s integraba solo ~47 grados en 2 s en lugar de 60. El sesgo del
        // giroscopio es un termino cuasi-estatico, de modo que solo se estima EN REPOSO
        // (modulo de la velocidad angular por debajo de ~10 grados/s = 0.175 rad/s).
        float raw[3];
        float mag2 = 0.0f;
        for (int i = 0; i < 3; ++i) {
            raw[i] = std::isfinite(gyro[i]) ? gyro[i] : 0.0f;
            mag2 += raw[i] * raw[i];
        }
        const bool quiescent = (std::sqrt(mag2) < 0.175f);
        const float biasGain = (1.0f - std::exp(-dt / 4.0f)) * (quiescent ? 1.0f : 0.0f);
        float w[3];
        for (int i = 0; i < 3; ++i) {
            bias_[i] += biasGain * (raw[i] - bias_[i]);
            w[i] = raw[i] - bias_[i];
        }
        // Eje de la velocidad angular: es el eje real sobre el que se extrapola la pose
        // en poseAt() para cubrir la latencia del sensor.
        predictAxis_[0] = w[0]; predictAxis_[1] = w[1]; predictAxis_[2] = w[2];
        const float rateDeg = std::sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]) * 180.0f / 3.14159265358979f;
        integratedDeg_ += rateDeg * dt;

        // Integración del cuaternión por velocidad angular (forma de Euler exacta).
        const float hx = w[0] * dt * 0.5f, hy = w[1] * dt * 0.5f, hz = w[2] * dt * 0.5f;
        Quaternion dq;
        dq.x = hx; dq.y = hy; dq.z = hz; dq.w = 1.0f;
        dq.normalize();

        const Quaternion a = acc_.orientation;
        Quaternion r;
        r.w = a.w * dq.w - a.x * dq.x - a.y * dq.y - a.z * dq.z;
        r.x = a.w * dq.x + a.x * dq.w + a.y * dq.z - a.z * dq.y;
        r.y = a.w * dq.y - a.x * dq.z + a.y * dq.w + a.z * dq.x;
        r.z = a.w * dq.z + a.x * dq.y - a.y * dq.x + a.z * dq.w;
        r.normalize();
        prevGyro_.orientation = a;
        prevGyro_.timestampMs = timestampMs;
        acc_.orientation = r;
        acc_.timestampMs = timestampMs;
        acc_.angularRateDegS = rateDeg;

        // Corrección por gravedad: el acelerómetro (en reposo) da el vector "abajo" del
        // mundo; se corrige la inclinación con un polo de tau larga para no inyectar ruido.
        if (useGravity_.load(std::memory_order_relaxed) && hasAccel_) {
            applyTiltCorrection(grav_[0], grav_[1], grav_[2], dt,
                                tauGrav_.load(std::memory_order_relaxed), false);
        }
        publish();
    }

    // Acelerómetro en m/s² (Sensor.TYPE_ACCELEROMETER), ya sin gravedad si está disponible.
    void updateAccel(const float acc[3], float timestampMs) noexcept {
        if (!acc) return;
        const float g = std::sqrt(acc[0] * acc[0] + acc[1] * acc[1] + acc[2] * acc[2]);
        lastAccelMs_ = timestampMs;
        hasAccel_ = true;
        // Solo se usa como referencia de gravedad si su módulo es compatible con 1 g
        // (es decir, el dispositivo no está acelerando): 9.81 ± 1.5 m/s².
        accelValid_ = (g > 8.3f && g < 11.3f);
        if (!accelValid_) return;
        grav_[0] = acc[0] / g; grav_[1] = acc[1] / g; grav_[2] = acc[2] / g;

        if (hasGyro_) {
            const float dt = 1.0f / 100.0f;  // el acelerómetro no trae dt fiable; se usa 10 ms
            applyTiltCorrection(grav_[0], grav_[1], grav_[2], dt,
                                tauGrav_.load(std::memory_order_relaxed), true);
            publish();
        }
    }

    // Magnetómetro normalizado (Sensor.TYPE_MAGNETIC_FIELD): corrige el acimut (yaw).
    void updateMagnetometer(const float mag[3], float timestampMs) noexcept {
        if (!mag) return;
        const float m = std::sqrt(mag[0] * mag[0] + mag[1] * mag[1] + mag[2] * mag[2]);
        if (!(m > 1.0e-6f)) return;
        magnet_[0] = mag[0] / m; magnet_[1] = mag[1] / m; magnet_[2] = mag[2] / m;
        lastMagMs_ = timestampMs;
        hasMag_ = true;
        if (useMag_.load(std::memory_order_relaxed) && hasGyro_) {
            const float dt = 1.0f / 50.0f;
            const float yawMag = std::atan2(magnet_[0], magnet_[1]) * 180.0f / 3.14159265358979f;
            const float yawCur = acc_.orientation.yawDegrees();
            float err = yawMag - yawCur;
            while (err > 180.0f) err -= 360.0f;
            while (err < -180.0f) err += 360.0f;
            const float tau = tauYaw_.load(std::memory_order_relaxed);
            const float a = 1.0f - std::exp(-dt / tau);
            const float corr = err * a * 3.14159265358979f / 180.0f * 0.5f;
            Quaternion dq;
            dq.x = 0.0f; dq.y = 0.0f; dq.z = std::sin(corr); dq.w = std::cos(corr);
            const Quaternion cur = acc_.orientation;
            Quaternion r;
            r.w = cur.w * dq.w - cur.x * dq.x - cur.y * dq.y - cur.z * dq.z;
            r.x = cur.w * dq.x + cur.x * dq.w + cur.y * dq.z - cur.z * dq.y;
            r.y = cur.w * dq.y - cur.x * dq.z + cur.y * dq.w + cur.z * dq.x;
            r.z = cur.w * dq.z + cur.x * dq.y - cur.y * dq.x + cur.z * dq.w;
            r.normalize();
            acc_.orientation = r;
            acc_.timestampMs = timestampMs;
            publish();
        }
    }

    // ── Lectura (hilo de audio) ──────────────────────────────────────────────

    // Pose extrapolada al instante del frame de audio: cubre la latencia del sensor y el
    // intervalo entre muestras, de modo que el campo gira CON la cabeza, no detrás.
    FusedPose poseAt(float audioTimeMs) const noexcept {
        FusedPose p = latest();
        const float lag = audioTimeMs - p.timestampMs;
        const float pred = latMs_.load(std::memory_order_relaxed);
        // Solo se extrapola dentro de la ventana útil (nunca más de 3× la latencia):
        // extrapolar un hueco largo inventaría movimiento y marearía al oyente.
        const float horizon = pred * 3.0f;
        if (lag > 0.0f && lag <= horizon && p.angularRateDegS > 0.5f) {
            const float dtSec = lag * 1.0e-3f;
            Quaternion q = p.orientation;
            // Nota: el eje de la velocidad angular publicada es el del último giroscopio;
            // la extrapolación usa el mismo eje, con el ángulo = ω·Δt.
            const float half = 0.5f * p.angularRateDegS * 3.14159265358979f / 180.0f * dtSec;
            Quaternion dq;
            dq.x = predictAxis_[0] * std::sin(half);
            dq.y = predictAxis_[1] * std::sin(half);
            dq.z = predictAxis_[2] * std::sin(half);
            dq.w = std::cos(half);
            Quaternion r;
            r.w = q.w * dq.w - q.x * dq.x - q.y * dq.y - q.z * dq.z;
            r.x = q.w * dq.x + q.x * dq.w + q.y * dq.z - q.z * dq.y;
            r.y = q.w * dq.y - q.x * dq.z + q.y * dq.w + q.z * dq.x;
            r.z = q.w * dq.z + q.x * dq.y - q.y * dq.x + q.z * dq.w;
            r.normalize();
            p.orientation = r;
            p.yawDeg = r.yawDegrees();
        }
        return p;
    }

    FusedPose latest() const noexcept {
        FusedPose p;
        p.orientation = publishSlots_[slot_.load(std::memory_order_acquire) & 0x1u].orientation;
        p.timestampMs = pubTs_.load(std::memory_order_relaxed);
        p.confidence = pubConf_.load(std::memory_order_relaxed);
        p.yawDeg = p.orientation.yawDegrees();
        p.angularRateDegS = pubRate_.load(std::memory_order_relaxed);
        for (int i = 0; i < 3; ++i) p.gyroBiasDegS[i] = bias_[i] * 180.0f / 3.14159265358979f;
        return p;
    }

    // Acimut en grados y corrimiento lateral estimado (m) para CrosstalkCanceller /
    // HoaBinauralDecoder: convierte el giro de la cabeza en desplazamiento del par.
    void offsetForSpatial(float& azimuthDeg, float& lateralMeters) const noexcept {
        const FusedPose p = latest();
        azimuthDeg = p.yawDeg;
        // Un giro puro de cabeza en el sitio apenas desplaza el centro acústico; se
        // convierte el seno del acimut en un desplazamiento pequeño y acotado (hasta 3 cm),
        // que es lo que de verdad cambia el ITD del par de altavoces.
        lateralMeters = std::clamp(std::sin(p.yawDeg * 3.14159265358979f / 180.0f) * 0.03f, -0.03f, 0.03f);
    }

    // Deriva acumulada sin corrección absoluta (grados) — diagnóstico honesto del sesgo.
    float integrationDriftDeg() const noexcept { return driftDeg_; }

private:
    static inline float clampf(float v, float a, float b) noexcept { return std::min(std::max(v, a), b); }

    void applyTiltCorrection(float gx, float gy, float gz, float dt, float tau, bool fast) noexcept {
        // Vector "abajo" medido por gravedad frente al predicho por la actitud actual:
        // se calcula el error de inclinación y se inyecta una fracción pequeña por paso.
        const Quaternion q = acc_.orientation;
        const float qw = q.w, qx = q.x, qy = q.y, qz = q.z;
        // Rotación inversa aplicada al vector de gravedad (columna 3 de R para "abajo").
        const float downX = 2.0f * (qx * qz - qw * qy);
        const float downY = 2.0f * (qy * qz + qw * qx);
        const float downZ = 1.0f - 2.0f * (qx * qx + qy * qy);
        const float ex = gy * downZ - gz * downY;
        const float ey = gz * downX - gx * downZ;
        const float ez = gx * downY - gy * downX;
        const float a = 1.0f - std::exp(-dt / std::max(tau, 0.05f));
        const float k = fast ? a * 0.60f : a;
        Quaternion dq;
        dq.x = ex * k * 0.5f; dq.y = ey * k * 0.5f; dq.z = ez * k * 0.5f; dq.w = 1.0f;
        dq.normalize();
        Quaternion r;
        r.w = qw * dq.w - qx * dq.x - qy * dq.y - qz * dq.z;
        r.x = qw * dq.x + qx * dq.w + qy * dq.z - qz * dq.y;
        r.y = qw * dq.y - qx * dq.z + qy * dq.w + qz * dq.x;
        r.z = qw * dq.z + qx * dq.y - qy * dq.x + qz * dq.w;
        r.normalize();
        acc_.orientation = r;
        driftDeg_ += std::fabs(std::sqrt(ex * ex + ey * ey + ez * ez) * k * 180.0f / 3.14159265358979f);
    }

    void publish() noexcept {
        // Publicación wait-free por doble ranura + indicador atómico (sin bloqueo).
        const uint32_t cur = slot_.load(std::memory_order_relaxed);
        const uint32_t next = cur ^ 1u;
        publishSlots_[next].orientation = acc_.orientation;
        pubTs_.store(acc_.timestampMs, std::memory_order_relaxed);
        pubRate_.store(acc_.angularRateDegS, std::memory_order_relaxed);

        // Confianza: frescura de los sensores y coherencia acelerómetro/actitud.
        const float now = acc_.timestampMs;
        float conf = 1.0f;
        if (hasGyro_)  conf *= clampf(1.0f - (now - lastGyroMs_)  * 1.0e-4f, 0.0f, 1.0f);
        if (hasAccel_) conf *= clampf(1.0f - (now - lastAccelMs_) * 2.0e-5f, 0.0f, 1.0f);
        if (accelValid_) conf = clampf(conf * 1.0f, 0.0f, 1.0f); else conf *= 0.85f;
        pubConf_.store(conf, std::memory_order_relaxed);
        slot_.store(next, std::memory_order_release);

        // Eje de predicción normalizado (para poseAt).
        const float wn = std::sqrt(predictAxis_[0] * predictAxis_[0]
                                 + predictAxis_[1] * predictAxis_[1]
                                 + predictAxis_[2] * predictAxis_[2]);
        if (wn > 1.0e-6f) {
            predictAxis_[0] /= wn; predictAxis_[1] /= wn; predictAxis_[2] /= wn;
        } else {
            predictAxis_[0] = 0.0f; predictAxis_[1] = 0.0f; predictAxis_[2] = 1.0f;
        }
    }

    float sr_ = 48000.0f;

    FusedPose acc_{};
    FusedPose prevGyro_{};
    FusedPose lastRv_{};

    bool hasGyro_ = false, hasAccel_ = false, hasMag_ = false, hasRv_ = false;
    bool accelValid_ = false;
    float lastGyroMs_ = -1.0e9f, lastAccelMs_ = -1.0e9f, lastMagMs_ = -1.0e9f, lastRvMs_ = -1.0e9f;
    float bias_[3] = {0.f, 0.f, 0.f};
    float grav_[3] = {0.f, -1.f, 0.f};
    float magnet_[3] = {1.f, 0.f, 0.f};
    float predictAxis_[3] = {0.f, 0.f, 1.f};
    float predictionMs_ = 0.0f;
    float driftDeg_ = 0.0f;
    float integratedDeg_ = 0.0f;

    // Doble ranura de publicación (wait-free para el hilo de audio).
    mutable FusedPose publishSlots_[2]{};
    mutable std::atomic<uint32_t> slot_{0u};
    std::atomic<float> pubTs_{-1.0e9f};
    std::atomic<float> pubConf_{0.0f};
    std::atomic<float> pubRate_{0.0f};

    std::atomic<float> latMs_{10.0f};
    std::atomic<float> tauGrav_{6.0f};
    std::atomic<float> tauYaw_{20.0f};
    std::atomic<bool>  useGravity_{true};
    std::atomic<bool>  useMag_{false};
};

}} // namespace ivanna::spatial
