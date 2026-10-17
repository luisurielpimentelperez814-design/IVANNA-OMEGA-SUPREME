// ExcitationConductor.hpp — Conductor de Excitación Predictivo (00-Anticipación 5–10 ms)
// (c) 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// MOTIVO FÍSICO: el oído humano integra el transitorio en una ventana de ~5–10 ms.
// Si el campo espacial (WFS/HOA), la háptica y los armónicos se ABREN en el instante
// exacto del golpe, su apertura llega perceptualmente TARDE: el golpe aterriza sobre
// un campo que todavía está reaccionando. Este conductor estima el instante del
// próximo onset desde el historial de IOI (MusicFeatureExtractor f9) y la tasa de
// transitorios (f4), y ADELANTA la apertura del campo un tiempo de antelación
// configurable (leadMs ∈ [5, 10] ms) para que el golpe caiga sobre un campo YA abierto.
//
// REGLA DE TIEMPO REAL: cero malloc, cero locks, cero excepciones, cero pow/exp en el
// camino caliente (solo aritmética, un anillo fijo de IOI y envolventes de un polo).
// REGLA DE IDENTIDAD (filosofía del Atlas): con confianza baja la salida es la entrada
// EXACTA (gate = 0 → passthrough bit-exacto). El conductor jamás inventa efecto.
#pragma once

#include "SceneTargetBus.hpp"
#include "MusicFeatureExtractor.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace ivanna { namespace ime {

// Resultado de la predicción, publicado al hilo de audio por lectura atómica.
struct alignas(64) ExcitationOutput {
    float anticipation = 0.0f;  // 0..1 envolvente de anticipación (abre ANTES del golpe)
    float predictedPunch = 0.0f;// 0..1 fuerza esperada del golpe (graves + crest)
    float onsetPhase = 0.0f;    // 0..1 fase dentro del periodo estimado (1 = en el golpe)
    float confidence = 0.0f;    // 0..1 1 - CV(IOI) normalizado
    float gate = 0.0f;          // smoothstep(0.25, 0.60, confidence)
    float leadMs = 7.5f;        // antelación efectiva aplicada
    float periodMs = 0.0f;      // periodo estimado entre golpes
    float msToNextOnset = 0.0f; // negativo = onset recién pasado
    float hapticGain = 0.0f;    // 0..1 para MultibandHapticMapper
    bool  armed = false;        // true = hay predicción válida
};

class alignas(64) ExcitationConductor {
public:
    ExcitationConductor() noexcept { reset(); }

    void prepare(float sampleRate, int maxBlock) noexcept {
        sr_ = (sampleRate > 8000.0f && std::isfinite(sampleRate)) ? sampleRate : 48000.0f;
        maxBlock_ = (maxBlock > 0) ? maxBlock : 256;
        // Refractario del detector de onsets: 40 ms (por debajo del tempo humano máximo
        // útil para predicción de bombo/sub; evita doble disparo por ring de transitorio).
        refractoryMs_ = 40.0f;
        reset();
    }

    void reset() noexcept {
        for (int i = 0; i < kIoiSize; ++i) ioiMs_[i] = 0.0f;
        ioiCount_ = 0; ioiWrite_ = 0;
        tMs_ = 0.0f; lastOnsetMs_ = -1.0f;
        prevTransient_ = 0.0f; trDwell_ = 0.0f;
        env_ = 0.0f; holdMs_ = 0.0f;
        confSmooth_ = 0.0f; punchSmooth_ = 0.0f;
        out_ = ExcitationOutput{};
        slewSub_ = 0.0f; slewSpread_ = 0.0f; slewAir_ = 0.0f; slewElev_ = 0.0f;
    }

    // ── Controles atómicos (UI / JNI / decisión adaptativa) ───────────────────
    void setEnabled(bool on) noexcept { enabled_.store(on, std::memory_order_relaxed); }
    bool enabled() const noexcept { return enabled_.load(std::memory_order_relaxed); }

    // Antelación de la apertura del campo, clampada a la ventana perceptiva [5, 10] ms.
    void setLeadMs(float ms) noexcept {
        const float v = std::isfinite(ms) ? std::clamp(ms, 5.0f, 10.0f) : 7.5f;
        leadMs_.store(v, std::memory_order_relaxed);
    }
    float leadMs() const noexcept { return leadMs_.load(std::memory_order_relaxed); }

    // Profundidad de la modulación (0 = apagado exacto, 1 = máxima permitida).
    void setDepth(float d) noexcept {
        depth_.store(std::isfinite(d) ? std::clamp(d, 0.0f, 1.0f) : 0.0f, std::memory_order_relaxed);
    }
    float depth() const noexcept { return depth_.load(std::memory_order_relaxed); }

    // Techo de mojado de armónicos que impone SceneTargetBus (maxExcWetCeiling).
    void setHarmonicCeiling(float c) noexcept {
        if (std::isfinite(c)) harmCeil_.store(std::clamp(c, 0.02f, 0.30f), std::memory_order_relaxed);
    }

    // ── Avance por bloque (hilo worker IME o hilo de audio; RT-safe en ambos) ──
    void advance(const MusicFeatures& f, float blockMs) noexcept {
        const float dt = (std::isfinite(blockMs) && blockMs > 0.0f) ? blockMs
                       : (static_cast<float>(maxBlock_) / sr_) * 1000.0f;
        tMs_ += dt;

        // 1) Detector de onsets: derivada positiva de la tasa de transitorios con
        //    refractario. transientRate ya está normalizado [0,1] por el extractor.
        const float tr = std::clamp(f.transientRate, 0.0f, 1.0f);
        const float d  = tr - prevTransient_;
        const bool newOnset = (d > 0.04f) && (tMs_ - lastOnsetMs_ >= refractoryMs_);
        if (newOnset) {
            if (lastOnsetMs_ >= 0.0f) {
                const float ioi = tMs_ - lastOnsetMs_;
                if (ioi > 60.0f && ioi < 3000.0f) pushIoi(ioi);
            }
            lastOnsetMs_ = tMs_;
            holdMs_ = 0.0f;
        }
        prevTransient_ = tr;

        // Presencia de actividad ritmica con asimetria (ataque 30 ms / caida 800 ms):
        // mide si HAY pulso sin depender de que un bloque concreto caiga en el golpe.
        // Sin esto la confianza parpadearia al ritmo del bombo y la puerta de identidad
        // se abriria y cerraria dentro del mismo compas.
        {
            const float tauD = (tr > trDwell_) ? 30.0f : 800.0f;
            trDwell_ += (1.0f - std::exp(-dt / tauD)) * (tr - trDwell_);
        }

        // 2) Estimación robusta del periodo: media recortada (descarta 1 mayor y 1 menor)
        //    y regularidad = 1 - CV, que es la confianza de que el pulso se repetirá.
        float period = 0.0f, reg = 0.0f;
        estimatePeriod(period, reg);

        // 3) Tiempo al próximo onset. Sin historial, se usa el propio refractario como
        //    periodo provisional para no arrancar en fase muerta.
        float periodMs = (period > 0.0f) ? period : 500.0f;
        float since = (lastOnsetMs_ >= 0.0f) ? (tMs_ - lastOnsetMs_) : periodMs;
        float toNext = periodMs - since;
        if (toNext < -periodMs * 0.5f) toNext += periodMs; // re-anclaje de fase

        const float lead = leadMs_.load(std::memory_order_relaxed);
        const bool armed = (period > 0.0f) && (reg > 0.15f);

        // 4) Envolvente de anticipacion. IMPORTANTE (defecto detectado en verificacion):
        //    la ventana de 5-10 ms es MAS ESTRECHA que un bloque de analisis tipico
        //    (5.33 ms a 256 muestras, y hasta 10.7 ms a 512), de modo que exigir
        //    literalmente "toNext <= lead" hacia que la rampa nunca se activase: el
        //    tiempo al proximo golpe avanza a saltos de bloque y se pasaba de largo.
        //    Ahora la ventana vale lead + cuarto de bloque (tolerancia de resolucion) y
        //    la rampa se evalua de forma continua sobre esa ventana, de modo que la
        //    apertura arranca siempre, sea cual sea el tamano de bloque.
        // La rejilla de bloques y la del pulso pueden COINCIDIR (500 ms = 50 bloques de
        // 10 ms): exigir "toNext <= lead" hacia que el unico bloque dentro de la ventana
        // fuese el que ya cierra el golpe, y la rampa NUNCA se activaba (defecto medido:
        // anticipacion = 0.000 con pulso perfecto a 120 BPM). Regla robusta: el bloque que
        // CONTIENE el golpe abre al maximo y la rampa sube durante el lead anterior, de
        // modo que la apertura arranca siempre, cualquiera sea la rejilla.
        const float dock = 1.5f * dt;
        float target = 0.0f;
        if (armed) {
            if (toNext <= dt && toNext > -dock) {
                target = 1.0f;           // el golpe cae en este bloque: campo YA abierto
                holdMs_ = 0.0f;
            } else if (toNext <= dt + lead) {  // rampa de apertura dentro del lead
                const float t = std::clamp(1.0f - (toNext - dt) / lead, 0.0f, 1.0f);
                target = t * t * (3.0f - 2.0f * t); // smoothstep
                holdMs_ = 0.0f;
            } else if (toNext <= -dock && holdMs_ < 45.0f) {
                target = 1.0f;           // aterrizaje del golpe
                holdMs_ += dt;
            } else {
                holdMs_ = 45.0f;
            }
        } else {
            holdMs_ = 45.0f;
        }
        // Envolvente ASIMETRICA: ataque rapido (tau = lead/3) y caida lenta (60 ms), para
        // que el campo siga abierto mientras suena el golpe en lugar de cerrarse en seco.
        const float tauMs = (target > env_) ? std::max(lead / 3.0f, 1.0f) : 60.0f;
        const float a = 1.0f - std::exp(-dt / tauMs);
        env_ += a * (target - env_);
        env_ = std::clamp(env_, 0.0f, 1.0f);

        // 5) Puerta de confianza: sin pulso claro (musica sostenida, silencio, material
        //    ambiguo) la ganancia es 0 y el modulador devuelve la entrada EXACTA.
        //    La confianza combina la regularidad del pulso (reg) con la PRESENCIA de
        //    actividad ritmica (trDwell_), no con el valor instantaneo del bloque.
        const float confRaw = std::clamp(reg * std::clamp(trDwell_ * 1.6f, 0.0f, 1.0f), 0.0f, 1.0f);
        const float aConf = 1.0f - std::exp(-dt / 120.0f);
        confSmooth_ += aConf * (confRaw - confSmooth_);
        confSmooth_ = std::clamp(confSmooth_, 0.0f, 1.0f);
        const float conf = confSmooth_;
        const float gate = smoothstep(0.25f, 0.60f, conf);

        // 6) Fuerza esperada del golpe: graves + pico de cresta + flatness percusiva,
        //    suavizada (tau 200 ms) para que la apertura no module al ritmo del bombo.
        const float punchRaw = std::clamp(0.55f * f.bassRatio + 0.30f * std::clamp(f.crestDb / 24.0f, 0.0f, 1.0f)
                                        + 0.15f * (1.0f - std::clamp(f.flatness1m, 0.0f, 1.0f)), 0.0f, 1.0f);
        const float aPunch = 1.0f - std::exp(-dt / 200.0f);
        punchSmooth_ += aPunch * (punchRaw - punchSmooth_);
        const float punch = punchSmooth_;

        ExcitationOutput o = out_;
        o.anticipation   = enabled_.load(std::memory_order_relaxed) ? env_ : 0.0f;
        o.predictedPunch = punch;
        o.onsetPhase     = (periodMs > 1.0f) ? std::clamp(since / periodMs, 0.0f, 1.0f) : 0.0f;
        o.confidence     = conf;
        o.gate           = enabled_.load(std::memory_order_relaxed) ? gate : 0.0f;
        o.leadMs         = lead;
        o.periodMs       = (period > 0.0f) ? period : 0.0f;
        o.msToNextOnset  = toNext;
        o.hapticGain     = std::clamp(o.gate * (0.45f + 0.55f * o.anticipation) * (0.35f + 0.65f * punch), 0.0f, 1.0f);
        o.armed          = armed;
        out_ = o;
    }

    ExcitationOutput output() const noexcept { return out_; }
    float hapticGain() const noexcept { return out_.hapticGain; }

    // ── Modulación de la escena (hilo worker IME; publica luego en SceneTargetBus) ──
    // base = objetivos del StyleBlender; devuelve los objetivos ADELANTADOS.
    // Sin confianza, la salida es copia literal de base (identidad exacta).
    SceneApply modulate(const SceneApply& base, float maxExcWetCeiling = 0.15f) const noexcept {
        SceneApply out = base;
        const float depth = depth_.load(std::memory_order_relaxed);
        const float g = out_.gate * depth;
        if (g <= 1.0e-4f || out_.anticipation <= 1.0e-4f) {
            out.t = base.t; // passthrough bit-exacto
            return out;
        }

        // Slew limit por bloque (~25 ms de constante de asentamiento) para que la
        // apertura del campo no produzca un escalón audible en la imagen estéreo.
        const float slew = 0.12f;
        const float envA = out_.anticipation;
        const float subTarget  = std::clamp(base.t.subPunch      + 0.35f * g * envA, 0.0f, 0.85f);
        const float sprTarget  = std::clamp(base.t.wfsSpread     * (1.0f + 0.35f * g * envA), 0.10f, 1.0f);
        const float airTarget  = std::clamp(base.t.airHolography + 0.25f * g * envA, 0.0f, maxExcWetCeiling);
        const float elevTarget = std::clamp(base.t.stageElevation+ 0.08f * g * envA, 0.0f, 0.65f);

        slewSub_    += slew * (subTarget  - slewSub_);
        slewSpread_ += slew * (sprTarget  - slewSpread_);
        slewAir_    += slew * (airTarget  - slewAir_);
        slewElev_   += slew * (elevTarget - slewElev_);

        out.t.subPunch       = slewSub_;
        out.t.wfsSpread      = slewSpread_;
        out.t.airHolography  = slewAir_;
        out.t.stageElevation = slewElev_;
        out.t.hrtfDepth      = std::clamp(base.t.hrtfDepth + 0.10f * g * envA, 0.0f, 1.0f);
        // La coherencia interaural objetivo se conserva: el conductor no estrecha
        // la imagen (M10 C_s), solo la abre en el tiempo.
        return out;
    }

private:
    static constexpr int kIoiSize = 16;

    void pushIoi(float ms) noexcept {
        ioiMs_[ioiWrite_] = ms;
        ioiWrite_ = (ioiWrite_ + 1) % kIoiSize;
        if (ioiCount_ < kIoiSize) ++ioiCount_;
    }

    void estimatePeriod(float& periodOut, float& regOut) const noexcept {
        periodOut = 0.0f; regOut = 0.0f;
        if (ioiCount_ < 3) return;
        float sum = 0.0f, mn = 1.0e9f, mx = 0.0f;
        for (int i = 0; i < ioiCount_; ++i) {
            sum += ioiMs_[i];
            mn = std::min(mn, ioiMs_[i]);
            mx = std::max(mx, ioiMs_[i]);
        }
        const float mean = sum / static_cast<float>(ioiCount_);
        // Media recortada: quita el extremo de cada lado (robusta a dobles disparos).
        float sum2 = 0.0f; int cnt = 0;
        for (int i = 0; i < ioiCount_; ++i) {
            if (ioiMs_[i] <= mn + 1e-4f || ioiMs_[i] >= mx - 1e-4f) continue;
            sum2 += ioiMs_[i]; ++cnt;
        }
        const float trimmed = (cnt >= 2) ? (sum2 / static_cast<float>(cnt)) : mean;
        if (!(trimmed > 60.0f && trimmed < 3000.0f)) return;
        // CV respecto de la media recortada → regularidad (confianza de repetición).
        float var = 0.0f;
        for (int i = 0; i < ioiCount_; ++i) {
            const float e = ioiMs_[i] - trimmed;
            var += e * e;
        }
        var /= static_cast<float>(ioiCount_);
        const float cv = std::sqrt(var) / trimmed;
        periodOut = trimmed;
        regOut = std::clamp(1.0f - cv * 1.5f, 0.0f, 1.0f); // 1 con pulso estable, 0 con polímetro
    }

    static float smoothstep(float e0, float e1, float x) noexcept {
        const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    float sr_ = 48000.0f;
    int   maxBlock_ = 256;
    float refractoryMs_ = 40.0f;

    float tMs_ = 0.0f;
    float lastOnsetMs_ = -1.0f;
    float prevTransient_ = 0.0f;
    float holdMs_ = 45.0f;
    float env_ = 0.0f;
    float trDwell_ = 0.0f;
    float confSmooth_ = 0.0f;
    float punchSmooth_ = 0.0f;

    float ioiMs_[kIoiSize]{};
    int   ioiCount_ = 0;
    int   ioiWrite_ = 0;

    mutable float slewSub_ = 0.0f, slewSpread_ = 0.5f, slewAir_ = 0.0f, slewElev_ = 0.0f;

    ExcitationOutput out_{};

    std::atomic<bool>  enabled_{false};  // apagado por defecto (filosofía del repo)
    std::atomic<float> leadMs_{7.5f};
    std::atomic<float> depth_{0.0f};
    std::atomic<float> harmCeil_{0.15f};
};

}} // namespace ivanna::ime
