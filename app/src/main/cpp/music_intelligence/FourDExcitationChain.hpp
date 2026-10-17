// FourDExcitationChain.hpp — Cadena 4D unificada de excitación (integración)
// (c) 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// Une las piezas para que el sistema entero reaccione ANTES del golpe, no después:
//
//   MusicFeatureExtractor ──► ExcitationConductor ──► SceneTargetBus.publish()   (campo abierto antes)
//                                    │
//                                    ├──► MultibandHapticMapper   (sub/cuerpo/transitorio/textura)
//                                    │        ▲
//   ExposureDosimeter ─── hapticDoseLoad() ───┘            (el oído gobierna la piel)
//                                    │
//                                    └──► CrosstalkCanceller.setHeadOffset()  (el campo rota con la cabeza)
//                                          HeadTrackerFusion.offsetForSpatial()
//
// REGLAS DE LA CASA QUE SE RESPETAN AQUÍ:
//   · Todo apagado por defecto: la cadena no altera un solo bit hasta que el usuario la enciende.
//   · Un solo publicador de escena: la cadena escribe el escenario una vez por bloque (wait-free).
//   · Un solo lugar de arbitraje: SceneTargetBus sigue siendo el único árbitro (HRTF > WFS > RIR).
//   · Cero malloc/locks en el camino de audio; la cadena se alimenta del hilo de análisis IME.
#pragma once

#include "MusicFeatureExtractor.hpp"
#include "ExcitationConductor.hpp"
#include "MultibandHapticMapper.hpp"
#include "../spatial/CrosstalkCanceller.hpp"
#include "../spatial/HeadTrackerFusion.hpp"
#include "../dsp/ExposureDosimeter.hpp"
#include <algorithm>
#include <atomic>
#include <cstdint>

namespace ivanna { namespace ime {

struct FourDStatus {
    bool  enabled = false;
    bool  armed = false;
    float anticipation = 0.0f;
    float confidence = 0.0f;
    float periodMs = 0.0f;
    float leadMs = 7.5f;
    float hapticTotal = 0.0f;
    uint16_t hapticAmplitude = 0;
    float exposureDose = 0.0f;
    float exposureLaeq = 0.0f;
    float recommendedAttenuationDb = 0.0f;
    float headYawDeg = 0.0f;
    float itdMicroseconds = 0.0f;
    uint32_t publishedScenes = 0;
};

class alignas(64) FourDExcitationChain {
public:
    void prepare(float sampleRate, int maxBlock) noexcept {
        sr_ = (sampleRate > 8000.0f && std::isfinite(sampleRate)) ? sampleRate : 48000.0f;
        maxBlock_ = (maxBlock > 0) ? maxBlock : 256;
        extractor_.prepare(sr_, maxBlock_);
        conductor_.prepare(sr_, maxBlock_);
        haptics_.prepare(sr_, maxBlock_);
        dosimeter_.prepare(sr_);
        xtc_.prepare(sr_, maxBlock_);
        tracker_.prepare(sr_);
        blockMs_ = (float)maxBlock_ / sr_ * 1000.0f;
        enabled_.store(false, std::memory_order_relaxed);
        published_ = 0;
    }

    void reset() noexcept {
        extractor_.reset();
        conductor_.reset();
        haptics_.reset();
        dosimeter_.reset();
        xtc_.reset();
        tracker_.reset();
        published_ = 0;
    }

    // ── Interruptor maestro y sub-controles ──────────────────────────────────
    void setEnabled(bool on) noexcept {
        enabled_.store(on, std::memory_order_relaxed);
        conductor_.setEnabled(on);
        haptics_.setEnabled(on && hapticsOn_.load(std::memory_order_relaxed));
        dosimeter_.setEnabled(on);
        xbEnabled();
    }
    bool enabled() const noexcept { return enabled_.load(std::memory_order_relaxed); }

    void setHapticsEnabled(bool on) noexcept {
        hapticsOn_.store(on, std::memory_order_relaxed);
        haptics_.setEnabled(on && enabled_.load(std::memory_order_relaxed));
    }
    void setHapticScale(float s) noexcept { haptics_.setScale(s); }
    void setOutputLatencyMs(float ms) noexcept { haptics_.setOutputLatencyMs(ms); }

    // Dosímetro: criterio (0 WHO, 1 NIOSH, 2 OSHA), calibración y umbral de actuación.
    void setDosimeterMode(int m) noexcept { dosimeter_.setMode(m); }
    void setDosimeterCalibration(float dbfsToSpl) noexcept { dosimeter_.setCalibration(dbfsToSpl); }

    // Cancelación de diafonía (solo tiene sentido por AUX/altavoces).
    void setCrosstalkEnabled(bool on) noexcept { xbEnabled_.store(on, std::memory_order_relaxed); xbEnabled(); }
    void setSpeakerGeometry(const spatial::CrosstalkCanceller::Geometry& g) noexcept { xtc_.setGeometry(g); }

    void setLeadMs(float ms) noexcept { conductor_.setLeadMs(ms); }
    void setDepth(float d) noexcept { conductor_.setDepth(d); }

    // ── Alimentación por bloque (hilo de análisis / captura) ─────────────────

    // analyze(): escucha la señal y actualiza features, predicción, háptica y dosis.
    // audio también alimenta el dosímetro (RT-safe) y el cancelador (si está en Ruta AUX).
    void analyze(const float* L, const float* R, int n) noexcept {
        if (!enabled_.load(std::memory_order_relaxed)) return;
        extractor_.processBlock(L, R, n);
        const MusicFeatures f = extractor_.features();

        conductor_.setHarmonicCeiling(bus_.maxExcWetCeiling());
        conductor_.advance(f, blockMs_);

        dosimeter_.feed(L, R, n);
        haptics_.setDoseLoad(dosimeter_.hapticDoseLoad());
        haptics_.update(f, conductor_.output(), blockMs_);

        // El giro de la cabeza desplaza el par virtual: el campo rota con el oyente.
        float az = 0.0f, lat = 0.0f;
        tracker_.offsetForSpatial(az, lat);
        xtc_.setHeadOffset(az, lat);
        lastYaw_ = az;
    }

    // Procesa el audio por el cancelador cuando la salida es por altavoces (AUX/DAC).
    void applyOutputStage(float* L, float* R, int n) noexcept {
        if (!enabled_.load(std::memory_order_relaxed)) return;
        if (xbEnabled_.load(std::memory_order_relaxed)) xtc_.processBlock(L, R, n);
    }

    // publishScene(): toma los objetivos que produjo el StyleBlender y publica la versión
    // ADELANTADA en SceneTargetBus, que es el único árbitro de la escena.
    void publishScene(const SceneApply& base) noexcept {
        if (!enabled_.load(std::memory_order_relaxed)) return;
        SceneApply out = conductor_.modulate(base, bus_.maxExcWetCeiling());
        out.gate = base.gate;
        out.conf = base.conf;
        out.styleIndex = base.styleIndex;
        out.presenceRatio = base.presenceRatio;
        out.flatness1m = base.flatness1m;
        out.lateRatio = base.lateRatio;
        bus_.publish(out);
        ++published_;
    }

    // ── Lecturas ─────────────────────────────────────────────────────────────
    FourDStatus status() const noexcept {
        FourDStatus s{};
        const ExcitationOutput e = conductor_.output();
        const HapticFrame h = haptics_.frame();
        s.enabled = enabled_.load(std::memory_order_relaxed);
        s.armed = e.armed;
        s.anticipation = e.anticipation;
        s.confidence = e.confidence;
        s.periodMs = e.periodMs;
        s.leadMs = e.leadMs;
        s.hapticTotal = h.total;
        s.hapticAmplitude = haptics_.aggregateAmplitude();
        s.exposureDose = dosimeter_.dose();
        s.exposureLaeq = dosimeter_.laeqDb();
        s.recommendedAttenuationDb = dosimeter_.recommendedAttenuationDb();
        s.headYawDeg = lastYaw_;
        s.itdMicroseconds = xtc_.itdMicroseconds();
        s.publishedScenes = published_;
        return s;
    }

    SceneTargetBus& bus() noexcept { return bus_; }
    // Estas lecturas devuelven POR VALOR: features()/output()/frame() de los modulos
    // devuelven copias, y devolver una referencia a esa copia dejaba una referencia
    // colgante (defecto real detectado al compilar el banco host con -Wall: el compilador
    // avisaba "returning reference to temporary"). Las estructuras son pequenas y
    // trivialmente copiables.
    MusicFeatures features() const noexcept { return extractor_.features(); }
    ExcitationOutput excitation() const noexcept { return conductor_.output(); }
    HapticFrame hapticFrame() const noexcept { return haptics_.frame(); }
    const dosing::ExposureDosimeter& dosimeter() const noexcept { return dosimeter_; }
    spatial::CrosstalkCanceller& crosstalk() noexcept { return xtc_; }
    spatial::HeadTrackerFusion& tracker() noexcept { return tracker_; }

private:
    void xbEnabled() noexcept {
        xtc_.setEngage(xbEnabled_.load(std::memory_order_relaxed)
                       && enabled_.load(std::memory_order_relaxed));
    }

    float sr_ = 48000.0f;
    int   maxBlock_ = 256;
    float blockMs_ = 5.33f;
    float lastYaw_ = 0.0f;
    uint32_t published_ = 0;

    MusicFeatureExtractor extractor_{};
    ExcitationConductor   conductor_{};
    MultibandHapticMapper haptics_{};
    dosing::ExposureDosimeter dosimeter_{};
    spatial::CrosstalkCanceller xtc_{};
    spatial::HeadTrackerFusion tracker_{};
    SceneTargetBus& bus_ = SceneTargetBus::instance();

    std::atomic<bool> enabled_{false};
    std::atomic<bool> hapticsOn_{false};
    std::atomic<bool> xbEnabled_{false};
};

}} // namespace ivanna::ime
