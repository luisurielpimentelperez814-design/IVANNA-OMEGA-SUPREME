// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#pragma once

/*
 * ============================================================
 * IVANNA OMEGA SUPREME — Synthetic HRTF generator
 *
 * Genera respuestas al impulso binaurales (HRIR) por MODELO
 * MATEMÁTICO genérico — no son datos medidos de ningún dataset
 * ni de ningún fabricante. Componentes:
 *
 *   1) ITD (Interaural Time Difference) — fórmula de Woodworth
 *      para cabeza esférica: τ(θ) = (r/c)·(θ + sin θ)   [θ≥0]
 *      r ≈ 0.0875 m (radio de cabeza promedio), c = 343 m/s.
 *
 *   2) Head shadowing — filtro paso-bajo de primer orden en el
 *      oído contralateral, con frecuencia de corte que baja
 *      conforme el ángulo crece (aproximación al difraction
 *      shadowing real de una esfera rígida).
 *
 *   3) Pinna notch aproximado — un notch espectral simple
 *      (resonador biquad tipo notch) en 7-9kHz, cuya profundidad
 *      escala con |sin θ| — aproxima el notch de pabellón que da
 *      la sensación de "delante/detrás" y externalización, sin
 *      pretender ser anatómicamente exacto.
 *
 * Esto NO sustituye datasets HRTF medidos (p.ej. KEMAR/CIPIC) —
 * es un modelo perceptual barato para dar sensación de
 * espacialidad real vía convolución, calibrable con 'aggressiveness'.
 * Si en el futuro se dispone de un dataset propio medido/licenciado,
 * esta clase es reemplazable sin tocar HRTFConvolver (misma interfaz:
 * generate(azimuthDeg) -> IR L/R).
 * ============================================================
 */

#include <cmath>
#include <vector>
#include <algorithm>

namespace ivanna {

struct HRIRPair {
    std::vector<float> L;
    std::vector<float> R;
};

class SyntheticHRTF {
public:
    // irLen: longitud de la respuesta al impulso (taps). 128 @ 48kHz ≈ 2.7ms
    // de "cola" — suficiente para el notch de pinna sin ser costoso.
    void init(uint32_t sampleRate, int irLen) {
        sr_ = (float)sampleRate;
        irLen_ = irLen;
    }

    // Genera el par de HRIR para un azimut dado (grados, -90..+90,
    // + = derecha) y una "agresividad" [0,1] que escala la profundidad
    // del shadowing/notch (mapeada desde el control de UI existente).
    HRIRPair generate(float azimuthDeg, float aggressiveness) const {
        // Guarda contra NaN/inf de entrada (p.ej. propagados desde un
        // head-tracker con timestamps duplicados): sin esto, theta se vuelve
        // NaN y contamina toda la IR -> convolución con NaN -> silencio/distorsión.
        if (!std::isfinite(azimuthDeg)) azimuthDeg = 0.f;
        if (!std::isfinite(aggressiveness)) aggressiveness = 0.5f;

        aggressiveness = std::clamp(aggressiveness, 0.f, 1.f);
        const float theta = azimuthDeg * (float)M_PI / 180.f;
        const float absTheta = std::fabs(theta);

        // ── 1) ITD (Woodworth) ──────────────────────────────────────────
        constexpr float HEAD_R = 0.0875f;   // m
        constexpr float SPEED  = 343.f;     // m/s
        const float tau = (HEAD_R / SPEED) * (absTheta + std::sin(absTheta)); // s
        const float itdSamples = tau * sr_;
        const int   delaySamp  = std::clamp((int)std::round(itdSamples), 0, irLen_ / 2);

        HRIRPair out;
        out.L.assign(irLen_, 0.f);
        out.R.assign(irLen_, 0.f);

        // Oído "cercano" (ipsilateral) sin retardo; "lejano" (contralateral)
        // retardado + atenuado (shadowing) + notch de pinna.
        const bool sourceRight = theta >= 0.f;
        std::vector<float>& nearEar = sourceRight ? out.R : out.L;
        std::vector<float>& farEar  = sourceRight ? out.L : out.R;

        // Impulso directo en oído cercano (ganancia unidad, sin retardo)
        nearEar[0] = 1.f;

        // ── 2) Head shadowing (paso-bajo IIR de 1er orden, aplicado como
        //      respuesta al impulso truncada) en el oído lejano ─────────
        // FIX: Shadowing menos agresivo para sonido más natural
        // Antes: bajaba de 16kHz a 1.5kHz (demasiado brusco)
        // Ahora: baja de 14kHz a 3.5kHz (más natural, preserva más riqueza)
        const float shadowAmount = (absTheta / (float)(M_PI * 0.5)) * aggressiveness;
        const float fc = 14000.f - shadowAmount * 10500.f;  // FIX: rango menos agresivo
        const float rc = 1.f / (2.f * (float)M_PI * fc);
        const float dt = 1.f / sr_;
        const float alpha = dt / (rc + dt);   // coeficiente paso-bajo

        // FIX: Ganancia DC más suave (no atenúa tanto)
        // Antes: hasta -50% ganancia
        // Ahora: hasta -30% ganancia (mejor balance)
        const float shadowGain = 1.f - 0.3f * shadowAmount;

        // Generamos la IR del paso-bajo de 1er orden truncada a irLen_ taps,
        // desplazada por el retardo ITD.
        float lpState = 0.f;
        for (int n = 0; n < irLen_; ++n) {
            const float impulse = (n == 0) ? 1.f : 0.f;
            lpState = lpState + alpha * (impulse - lpState);
            const int idx = n + delaySamp;
            if (idx < irLen_) farEar[idx] += lpState * shadowGain;
        }

        // ── 3) Pinna notch aproximado — biquad notch aplicado como
        //      convolución corta (3 taps) centrada en ~7.5kHz, profundidad
        //      escalada por |sin theta| * aggressiveness ────────────────
        const float notchDepth = std::fabs(std::sin(theta)) * aggressiveness * 0.6f;
        if (notchDepth > 0.001f) {
            apply_notch_fir(nearEar, 7500.f, notchDepth);
            apply_notch_fir(farEar,  7500.f, notchDepth * 0.7f);
        }

        return out;
    }

private:
    // FIX: Notch FIR mejorado con mejor balance
    // El notch anterior causaba artefactos audibles (cambios bruscos de ganancia)
    // Ahora usa kernel normalizado + notch más suave
    void apply_notch_fir(std::vector<float>& buf, float freqHz, float depth) const {
        depth = std::clamp(depth, 0.f, 0.6f);  // Limitar profundidad para evitar notch excesivo
        
        const float w0 = 2.f * (float)M_PI * freqHz / sr_;
        const float cosw0 = std::cos(w0);
        
        // FIX: Kernel notch mejorado (simétrico, mejor comportamiento en fase)
        // [a, b, a] donde: a controla profundidad, b = 2*(1-a)
        const float a = depth * 0.2f;  // Reducir sensibilidad a depth
        const float b = 2.f * (1.f - a * cosw0);
        
        // Normalización para mantener DC gain = 1
        const float norm = a + b + a;  // = 2a + 2(1-a*cosw0) = 2(1 - a*cosw0 + a) 
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
};

} // namespace ivanna
