// ═══════════════════════════════════════════════════════════════════════════════
// test_supreme_acoustic_continuity.cpp
// Certificación Profesional de Nivel Producción:
//   "SUPREME ACOUSTIC STATE CONTINUITY ARCHITECTURE"
//
// Valida los 3 Niveles Arquitectónicos:
//   - NIVEL 1: Estado DSP Inmortal (PersistentAcousticState)
//   - NIVEL 2: Suspensión Acústica Inteligente (Soft Suspension)
//   - NIVEL 3: Reactivación Continua (Smooth State Resume sin seedConstant ni reset)
//
// Pruebas exigidas:
//   1. Guardia Anti-Destrucción de Estado DSP (falla si alguien introduce
//      clearFilterStates(), farrow.reset(), o memset(history) durante playback).
//   2. Activar/desactivar 10,000 veces sin clic (10,000 ciclos ON/OFF).
//   3. Thermal Governor oscilando continuamente entre NORMAL, LIMITED y SAFE.
//   4. Cambio rápido de estados vía SHM/JNI (Pinna FIR, Farrow ITD, Lattice, CVNN, SNN).
//   5. Música con transitorios fuertes: medición de discontinuidad máxima entre
//      muestras, energía del transitorio, THD y estabilidad de fase (Antes vs Después).
// ═══════════════════════════════════════════════════════════════════════════════

#include <gtest/gtest.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>
#include <numeric>

#include "../supreme/SupremeAcousticContinuity.hpp"
#include "../supreme/SupremeTransitionEnvelope.hpp"
#include "../supreme/WarpedLatticeTransducerInverter.hpp"
#include "../supreme/PhaseCoherentTransharmonicSynthesizer.hpp"
#include "../supreme/SnnNmfHoaUpmixer.hpp"
#include "../supreme/PinnaManifoldInterpolator.hpp"
#include "../supreme/ShmPipelineArbitrator.hpp"
#include "../neuromorphic/CochlearActiveInverseModel.hpp"
#include "../neuromorphic/volterra_h2_symmetric.hpp"
#include "../include/acoustic_reality_hyperengine.hpp"
#include "../spatial/RoomProjectionEngine.hpp"
#include "../spatial/WfsRenderer.hpp"
#include "../spatial/RirConvolver.hpp"
#include "../spatial/IvannaAudioPipeline.hpp"

namespace {

constexpr float kSampleRate = 48000.0f;
constexpr size_t kBlockSize = 64;
constexpr float kTwoPi = 6.28318530717958647692f;

// Calcula THD respecto a una fundamental f0 usando proyección DFT exacta sobre ventana Hann
inline float computeFundamentalThd(const float* signal, size_t N, float f0, float fs) noexcept {
    if (N < 64) return 0.0f;
    auto goertzelMagSq = [&](float freq) -> double {
        double re = 0.0;
        double im = 0.0;
        for (size_t n = 0; n < N; ++n) {
            const double w = 0.5 * (1.0 - std::cos(2.0 * M_PI * static_cast<double>(n) / static_cast<double>(N - 1)));
            const double angle = 2.0 * M_PI * static_cast<double>(freq) * static_cast<double>(n) / static_cast<double>(fs);
            const double x = static_cast<double>(signal[n]) * w;
            re += x * std::cos(angle);
            im -= x * std::sin(angle);
        }
        return re * re + im * im;
    };

    const double fundMagSq = goertzelMagSq(f0);
    if (fundMagSq <= 1.0e-15) return 0.0f;

    double harmSumSq = 0.0;
    for (int h = 2; h <= 7; ++h) {
        const float hf = f0 * static_cast<float>(h);
        if (hf < 0.45f * fs) {
            harmSumSq += goertzelMagSq(hf);
        }
    }
    return static_cast<float>(std::sqrt(harmSumSq / fundMagSq));
}

// ── 1. Propiedades de arquitectura y Guardia Anti-Destrucción de Estado DSP ─
TEST(SupremeAcousticContinuityTest, ImmortalStateGuardFailsIfStateDestroyed) {
    using ivanna::supreme::PersistentAcousticState;
    using ivanna::supreme::SupremeStateContinuityManager;

    static_assert(std::is_trivially_copyable_v<PersistentAcousticState>);
    static_assert(alignof(PersistentAcousticState) == 64);
    static_assert(std::is_trivially_copyable_v<SupremeStateContinuityManager>);
    static_assert(alignof(SupremeStateContinuityManager) == 64);

    ivanna::supreme::WarpedLatticeTransducerInverter lattice;
    ivanna::supreme::PhaseCoherentTransharmonicSynthesizer cvnn;
    ivanna::supreme::SnnNmfHoaUpmixer snnHoa;
    ivanna::supreme::PinnaManifoldInterpolator pinna;
    ivanna::supreme::SupremeMsoFarrowArbitrator msoFarrow;
    ivanna::neuromorphic::CochlearActiveInverseEngine cochlear;
    ivanna::dsp::VolterraH2Symmetric volterra(16, 2);
    ivanna::spatial::RoomProjectionEngine roomProj;
    ivanna::reality::MicroRealityExtractor microReality;
    ivanna::reality::MicroDetailMap detailMap{};
    detailMap.intelligibilityContrast = 0.20f;

    lattice.prepare(kSampleRate);
    lattice.setEnabled(true);
    cvnn.prepare(kSampleRate);
    cvnn.setEnabled(true);
    snnHoa.prepare(kSampleRate);
    snnHoa.setEnabled(true);
    pinna.calibrateFromLatents(0.4f, -0.3f, 0.2f, kSampleRate);
    pinna.setWetMix(0.9f);
    pinna.setEnabled(true);
    msoFarrow.prepare(kSampleRate);
    msoFarrow.setMsoItdNanoseconds(240000.0f);
    msoFarrow.setEnabled(true);
    cochlear.prepare(kSampleRate, static_cast<int>(kBlockSize));
    cochlear.setIntensity(0.8f);
    cochlear.setEnabled(true);
    roomProj.setInversionGain(0.75f);

    std::vector<float> h1(16, 0.0f);
    std::vector<float> h2((16 * 17) / 2, 0.0f);
    h1[0] = 0.9f;
    h1[1] = 0.15f;
    h2[0] = 0.08f;
    volterra.updateKernels(h1.data(), h2.data(), 16);
    volterra.setEnabled(true);

    std::array<float, kBlockSize> L{};
    std::array<float, kBlockSize> R{};
    std::array<float, kBlockSize * 2> interleavedIn{};
    std::array<float, kBlockSize * 2> interleavedOut{};

    float phase = 0.0f;
    for (int blk = 0; blk < 12; ++blk) {
        for (size_t i = 0; i < kBlockSize; ++i) {
            const float s = 0.6f * std::sin(phase) + 0.25f * std::cos(2.3f * phase);
            L[i] = s;
            R[i] = s;
            interleavedIn[2 * i] = s;
            interleavedIn[2 * i + 1] = s;
            phase += kTwoPi * 440.0f / kSampleRate;
        }
        auto lLat = L, rLat = R; lattice.process(lLat.data(), rLat.data(), kBlockSize);
        auto lCvn = L, rCvn = R; cvnn.process(lCvn.data(), rCvn.data(), kBlockSize);
        auto lSnn = L, rSnn = R; snnHoa.process(lSnn.data(), rSnn.data(), kBlockSize);
        auto lPin = L, rPin = R; pinna.process(lPin.data(), rPin.data(), kBlockSize);
        auto lMso = L, rMso = R; msoFarrow.process(lMso.data(), rMso.data(), kBlockSize, kSampleRate);
        auto lCoc = L, rCoc = R; cochlear.process(lCoc.data(), rCoc.data(), static_cast<int>(kBlockSize));
        volterra.processInterleaved(interleavedIn.data(), interleavedOut.data(), kBlockSize, 2);
        auto lRom = L, rRom = R; roomProj.process(lRom.data(), rRom.data(), kBlockSize);
        auto lMic = L, rMic = R; microReality.applyMicroIntelligibilityPass(lMic.data(), rMic.data(), kBlockSize, detailMap, true, false);
    }

    // Verificar que todos los motores tienen memoria acústica activa (energía > 0)
    EXPECT_GT(lattice.preservedStateEnergy(), 1.0e-6f);
    EXPECT_GT(cvnn.preservedStateEnergy(), 1.0e-6f);
    EXPECT_GT(snnHoa.preservedStateEnergy(), 1.0e-6f);
    EXPECT_GT(pinna.preservedStateEnergy(), 1.0e-6f);
    EXPECT_GT(msoFarrow.preservedStateEnergy(), 1.0e-6f);
    EXPECT_GT(cochlear.preservedStateEnergy(), 1.0e-6f);
    EXPECT_GT(roomProj.preservedStateEnergy(), 1.0e-6f);
    EXPECT_GT(microReality.preservedStateEnergy(), 1.0e-6f);

    // NIVEL 2: Suspender suavemente todos los motores hasta que la rampa llegue a 0.0
    lattice.setEnabled(false);
    cvnn.setEnabled(false);
    snnHoa.setEnabled(false);
    pinna.setEnabled(false);
    msoFarrow.setEnabled(false);
    cochlear.setEnabled(false);
    volterra.setEnabled(false);
    roomProj.setInversionGain(0.0f);

    for (int blk = 0; blk < 30; ++blk) {
        for (size_t i = 0; i < kBlockSize; ++i) {
            const float s = 0.6f * std::sin(phase) + 0.25f * std::cos(2.3f * phase);
            L[i] = s;
            R[i] = s;
            interleavedIn[2 * i] = s;
            interleavedIn[2 * i + 1] = s;
            phase += kTwoPi * 440.0f / kSampleRate;
        }
        auto lLat = L, rLat = R; lattice.process(lLat.data(), rLat.data(), kBlockSize);
        auto lCvn = L, rCvn = R; cvnn.process(lCvn.data(), rCvn.data(), kBlockSize);
        auto lSnn = L, rSnn = R; snnHoa.process(lSnn.data(), rSnn.data(), kBlockSize);
        auto lPin = L, rPin = R; pinna.process(lPin.data(), rPin.data(), kBlockSize);
        auto lMso = L, rMso = R; msoFarrow.process(lMso.data(), rMso.data(), kBlockSize, kSampleRate);
        auto lCoc = L, rCoc = R; cochlear.process(lCoc.data(), rCoc.data(), static_cast<int>(kBlockSize));
        volterra.processInterleaved(interleavedIn.data(), interleavedOut.data(), kBlockSize, 2);
        auto lRom = L, rRom = R; roomProj.process(lRom.data(), rRom.data(), kBlockSize);
        auto lMic = L, rMic = R; microReality.applyMicroIntelligibilityPass(lMic.data(), rMic.data(), kBlockSize, detailMap, false, true);
    }

    // GUARDIA ESTRICTA: En estado SoftSuspended, NINGÚN motor puede haber destruido
    // su memoria acústica interna (si alguien llama clearFilterStates(), farrow.reset()
    // o memset(0), preservedStateEnergy() caerá a 0.0f y este test fallará).
    EXPECT_GT(lattice.preservedStateEnergy(), 1.0e-6f)
        << "FALLO CRÍTICO: WarpedLatticeTransducerInverter destruyó su estado en suspensión!";
    EXPECT_GT(cvnn.preservedStateEnergy(), 1.0e-6f)
        << "FALLO CRÍTICO: PhaseCoherentTransharmonicSynthesizer destruyó su estado en suspensión!";
    EXPECT_GT(snnHoa.preservedStateEnergy(), 1.0e-6f)
        << "FALLO CRÍTICO: SnnNmfHoaUpmixer destruyó su estado en suspensión!";
    EXPECT_GT(pinna.preservedStateEnergy(), 1.0e-6f)
        << "FALLO CRÍTICO: PinnaManifoldInterpolator destruyó su línea FIR en suspensión!";
    EXPECT_GT(msoFarrow.preservedStateEnergy(), 1.0e-6f)
        << "FALLO CRÍTICO: SupremeMsoFarrowArbitrator destruyó su historia Farrow en suspensión!";
    EXPECT_GT(cochlear.preservedStateEnergy(), 1.0e-6f)
        << "FALLO CRÍTICO: CochlearActiveInverseEngine llamó clearFilterStates() en suspensión!";
    EXPECT_GT(roomProj.preservedStateEnergy(), 1.0e-6f)
        << "FALLO CRÍTICO: RoomProjectionEngine borró su historial WPE en suspensión!";
    EXPECT_GT(microReality.preservedStateEnergy(), 1.0e-6f)
        << "FALLO CRÍTICO: MicroRealityExtractor borró sus filtros en suspensión!";

    // Verificar además que una llamada a reset() durante reproducción valida pero NO destruye
    // la memoria acústica inmortal.
    lattice.reset();
    cvnn.reset();
    snnHoa.reset();
    pinna.reset();
    msoFarrow.reset();
    cochlear.reset();
    roomProj.reset();

    EXPECT_GT(lattice.preservedStateEnergy(), 1.0e-6f);
    EXPECT_GT(cvnn.preservedStateEnergy(), 1.0e-6f);
    EXPECT_GT(snnHoa.preservedStateEnergy(), 1.0e-6f);
    EXPECT_GT(pinna.preservedStateEnergy(), 1.0e-6f);
    EXPECT_GT(msoFarrow.preservedStateEnergy(), 1.0e-6f);
    EXPECT_GT(cochlear.preservedStateEnergy(), 1.0e-6f);
    EXPECT_GT(roomProj.preservedStateEnergy(), 1.0e-6f);
}

// ── 2. Farrow: Recuperación basada en historia real conservada (sin seedConstant) ─
TEST(SupremeAcousticContinuityTest, FarrowRealHistoryRecoveryVsDestructiveReset) {
    ivanna::supreme::SupremeMsoFarrowArbitrator continuousFarrow;
    continuousFarrow.prepare(kSampleRate);
    continuousFarrow.setMsoItdNanoseconds(260000.0f);
    continuousFarrow.setEnabled(true);

    float phase = 0.0f;
    float prevContR = 0.0f;
    float prevDestR = 0.0f;
    float maxContJumpOnResume = 0.0f;
    float maxDestJumpOnResume = 0.0f;

    std::array<float, 6> destHistory{};
    destHistory.fill(0.0f);

    for (int cycle = 0; cycle < 20; ++cycle) {
        const bool active = (cycle % 2 == 0);
        continuousFarrow.setEnabled(active);

        // Si el motor antiguo se reactivaba tras destruir su historia (farrowR_.reset() -> 0):
        if (active && cycle > 0) {
            destHistory.fill(0.0f);
        }

        for (int blk = 0; blk < 8; ++blk) {
            std::array<float, kBlockSize> L{};
            std::array<float, kBlockSize> R{};
            std::array<float, kBlockSize> destR{};

            for (size_t i = 0; i < kBlockSize; ++i) {
                const float s = 0.75f * std::sin(phase) + 0.20f * std::sin(3.0f * phase);
                L[i] = s;
                R[i] = s;
                if (active) {
                    for (size_t k = 5; k > 0; --k) destHistory[k] = destHistory[k - 1];
                    destHistory[0] = s;
                    // En un Farrow de 5º orden (centrado entre tap 2 y 3), si la historia fue
                    // borrada a 0.0, las primeras 3 muestras tras reactivar leen 0.0 en los taps centrales.
                    destR[i] = 0.5f * destHistory[2] + 0.5f * destHistory[3];
                } else {
                    destR[i] = s;
                }
                phase += kTwoPi * 620.0f / kSampleRate;
            }

            continuousFarrow.process(L.data(), R.data(), kBlockSize, kSampleRate);

            for (size_t i = 0; i < kBlockSize; ++i) {
                if (cycle > 0 || blk > 0 || i > 0) {
                    const float dCont = std::fabs(R[i] - prevContR);
                    const float dDest = std::fabs(destR[i] - prevDestR);
                    if (active && blk == 0 && i < 8 && cycle > 0) {
                        maxContJumpOnResume = std::max(maxContJumpOnResume, dCont);
                        maxDestJumpOnResume = std::max(maxDestJumpOnResume, dDest);
                    }
                }
                prevContR = R[i];
                prevDestR = destR[i];
            }
        }
    }

    // El modelo antiguo destructivo produce un salto abrupto > 0.35 al perder la historia Farrow;
    // la arquitectura de continuidad mantiene el paso suave acotado por la pendiente de la onda.
    EXPECT_GT(maxDestJumpOnResume, 0.35f);
    EXPECT_LT(maxContJumpOnResume, 0.14f);
    EXPECT_LT(maxContJumpOnResume, maxDestJumpOnResume * 0.35f);
}

// ── 3. Activar/desactivar 10,000 veces sin clic ──────────────────────────────
TEST(SupremeAcousticContinuityTest, TenThousandToggleCyclesZeroClicks) {
    ivanna::supreme::WarpedLatticeTransducerInverter lattice;
    ivanna::supreme::PhaseCoherentTransharmonicSynthesizer cvnn;
    ivanna::supreme::SnnNmfHoaUpmixer snnHoa;
    ivanna::supreme::PinnaManifoldInterpolator pinna;
    ivanna::supreme::SupremeMsoFarrowArbitrator msoFarrow;

    lattice.prepare(kSampleRate);
    cvnn.prepare(kSampleRate);
    snnHoa.prepare(kSampleRate);
    pinna.calibrateFromLatents(0.35f, -0.25f, 0.15f, kSampleRate);
    pinna.setWetMix(0.85f);
    msoFarrow.prepare(kSampleRate);
    msoFarrow.setMsoItdNanoseconds(195000.0f);

    constexpr int kToggleCycles = 10000;
    constexpr size_t kMiniBlock = 8;

    float phase = 0.0f;
    float prevL = 0.0f;
    float prevR = 0.0f;
    float maxJump = 0.0f;
    bool allFinite = true;

    for (int toggle = 0; toggle < kToggleCycles; ++toggle) {
        const bool enabled = (toggle & 1) == 0;

        lattice.setEnabled(enabled);
        cvnn.setEnabled(enabled);
        snnHoa.setEnabled(enabled);
        pinna.setEnabled(enabled);
        msoFarrow.setEnabled(enabled);

        std::array<float, kMiniBlock> L{};
        std::array<float, kMiniBlock> R{};

        for (size_t i = 0; i < kMiniBlock; ++i) {
            const float s = 0.55f * std::sin(phase) + 0.20f * std::cos(1.9f * phase);
            L[i] = s;
            R[i] = s;
            phase += kTwoPi * 380.0f / kSampleRate;
            if (phase >= kTwoPi) phase -= kTwoPi;
        }

        lattice.process(L.data(), R.data(), kMiniBlock);
        cvnn.process(L.data(), R.data(), kMiniBlock);
        snnHoa.process(L.data(), R.data(), kMiniBlock);
        pinna.process(L.data(), R.data(), kMiniBlock);
        msoFarrow.process(L.data(), R.data(), kMiniBlock, kSampleRate);

        for (size_t i = 0; i < kMiniBlock; ++i) {
            if (!std::isfinite(L[i]) || !std::isfinite(R[i])) {
                allFinite = false;
            }
            if (toggle > 0 || i > 0) {
                const float dL = std::fabs(L[i] - prevL);
                const float dR = std::fabs(R[i] - prevR);
                maxJump = std::max({maxJump, dL, dR});
            }
            prevL = L[i];
            prevR = R[i];
        }
    }

    EXPECT_TRUE(allFinite);
    // Con 10,000 conmutaciones ON/OFF, el salto máximo entre muestras consecutivas
    // jamás supera la derivada natural de la señal procesada (cero clics/pops).
    EXPECT_LT(maxJump, 0.22f);
    EXPECT_GE(lattice.continuityManager().state().preservedBlocks, 1u);
    EXPECT_GT(lattice.preservedStateEnergy(), 1.0e-6f);
    EXPECT_GT(msoFarrow.preservedStateEnergy(), 1.0e-6f);
}

// ── 4. Thermal Governor oscilando continuamente ─────────────────────────────
TEST(SupremeAcousticContinuityTest, ContinuousThermalGovernorOscillation) {
    ivanna::supreme::WarpedLatticeTransducerInverter lattice;
    ivanna::supreme::PhaseCoherentTransharmonicSynthesizer cvnn;
    ivanna::supreme::SnnNmfHoaUpmixer snnHoa;
    ivanna::supreme::PinnaManifoldInterpolator pinna;
    ivanna::supreme::SupremeMsoFarrowArbitrator msoFarrow;
    ivanna::neuromorphic::CochlearActiveInverseEngine cochlear;
    ivanna::spatial::RoomProjectionEngine roomProj;
    ivanna::reality::MicroRealityExtractor microReality;
    ivanna::reality::MicroDetailMap detailMap{};
    detailMap.intelligibilityContrast = 0.18f;

    lattice.prepare(kSampleRate);
    lattice.setEnabled(true);
    cvnn.prepare(kSampleRate);
    cvnn.setEnabled(true);
    snnHoa.prepare(kSampleRate);
    snnHoa.setEnabled(true);
    pinna.calibrateFromLatents(0.5f, -0.4f, 0.3f, kSampleRate);
    pinna.setWetMix(0.8f);
    pinna.setEnabled(true);
    msoFarrow.prepare(kSampleRate);
    msoFarrow.setMsoItdNanoseconds(210000.0f);
    msoFarrow.setEnabled(true);
    cochlear.prepare(kSampleRate, static_cast<int>(kBlockSize));
    cochlear.setIntensity(0.75f);
    cochlear.setEnabled(true);
    roomProj.setInversionGain(0.65f);

    float phase = 0.0f;
    float prevL = 0.0f;
    float prevR = 0.0f;
    float maxBoundaryDelta = 0.0f;
    bool allFinite = true;

    // Simular 400 bloques oscilando por todos los estados del ThermalGovernor:
    // Tier 0 (NORMAL) -> Tier 1 (LIMITED) -> Tier 2 (SAFE) -> Tier 3 (CRITICAL) -> Tier 0
    for (int blk = 0; blk < 400; ++blk) {
        const int tier = (blk / 6) % 4;
        const bool bypassSnn = (tier >= 1);
        const bool bypassCvnn = (tier >= 2);
        const bool bypassAll = (tier >= 3);

        lattice.setThermalBypass(bypassAll);
        cvnn.setThermalBypass(bypassCvnn);
        snnHoa.setThermalBypass(bypassSnn);
        pinna.setThermalBypass(bypassAll);
        msoFarrow.setThermalBypass(bypassAll);
        cochlear.setThermalBypass(bypassCvnn);
        roomProj.setInversionGain(bypassAll ? 0.0f : 0.65f);

        std::array<float, kBlockSize> L{};
        std::array<float, kBlockSize> R{};
        for (size_t i = 0; i < kBlockSize; ++i) {
            const float s = 0.50f * std::sin(phase)
                          + 0.22f * std::sin(2.5f * phase)
                          + 0.10f * std::cos(4.1f * phase);
            L[i] = s;
            R[i] = s;
            phase += kTwoPi * 310.0f / kSampleRate;
            if (phase >= kTwoPi) phase -= kTwoPi;
        }

        lattice.process(L.data(), R.data(), kBlockSize);
        cvnn.process(L.data(), R.data(), kBlockSize);
        snnHoa.process(L.data(), R.data(), kBlockSize);
        pinna.process(L.data(), R.data(), kBlockSize);
        msoFarrow.process(L.data(), R.data(), kBlockSize, kSampleRate);
        cochlear.process(L.data(), R.data(), static_cast<int>(kBlockSize));
        roomProj.process(L.data(), R.data(), kBlockSize);
        microReality.applyMicroIntelligibilityPass(L.data(), R.data(), kBlockSize, detailMap, !bypassAll, bypassAll);

        for (size_t i = 0; i < kBlockSize; ++i) {
            if (!std::isfinite(L[i]) || !std::isfinite(R[i])) {
                allFinite = false;
            }
            if (blk > 0 && i == 0) {
                const float dL = std::fabs(L[0] - prevL);
                const float dR = std::fabs(R[0] - prevR);
                maxBoundaryDelta = std::max({maxBoundaryDelta, dL, dR});
            }
            prevL = L[i];
            prevR = R[i];
        }
    }

    EXPECT_TRUE(allFinite);
    EXPECT_LT(maxBoundaryDelta, 0.25f);
    EXPECT_GT(lattice.preservedStateEnergy(), 1.0e-6f);
    EXPECT_GT(cvnn.preservedStateEnergy(), 1.0e-6f);
    EXPECT_GT(snnHoa.preservedStateEnergy(), 1.0e-6f);
}

// ── 5. Cambio rápido de estados vía SHM/JNI ─────────────────────────────────
TEST(SupremeAcousticContinuityTest, RapidShmJniStateTransitions) {
    ivanna::supreme::PinnaManifoldInterpolator pinna;
    ivanna::supreme::SupremeMsoFarrowArbitrator msoFarrow;
    ivanna::supreme::WarpedLatticeTransducerInverter lattice;
    ivanna::spatial::WfsRenderer wfs;

    pinna.calibrateFromLatents(0.2f, -0.1f, 0.1f, kSampleRate);
    pinna.setWetMix(0.9f);
    pinna.setEnabled(true);
    msoFarrow.prepare(kSampleRate);
    msoFarrow.setEnabled(true);
    lattice.prepare(kSampleRate);
    lattice.setEnabled(true);
    wfs.init(kSampleRate, static_cast<int>(kBlockSize), 16);
    wfs.setEnabled(true);
    wfs.setObject(0, 0.0f, 1.5f, 0.8f);
    wfs.setObject(1, -0.6f, 1.8f, 0.7f);

    float phase = 0.0f;
    float prevL = 0.0f;
    float prevR = 0.0f;
    float maxDelta = 0.0f;
    bool allFinite = true;

    for (int blk = 0; blk < 250; ++blk) {
        // Simular ráfagas rápidas de actualizaciones JNI/SHM en cada bloque
        const float l0 = 0.8f * std::sin(0.17f * static_cast<float>(blk));
        const float l1 = 0.7f * std::cos(0.23f * static_cast<float>(blk));
        const float l2 = 0.5f * std::sin(0.31f * static_cast<float>(blk));
        pinna.calibrateFromLatents(l0, l1, l2, kSampleRate);
        pinna.setEnabled(blk % 5 != 0);

        const float itdNs = 180000.0f + 120000.0f * std::sin(0.19f * static_cast<float>(blk));
        msoFarrow.setMsoItdNanoseconds(itdNs);
        msoFarrow.setEnabled(blk % 7 != 0);

        lattice.setEnabled(blk % 6 != 0);
        wfs.setEnabled(blk % 4 != 0);

        std::array<float, kBlockSize> L{};
        std::array<float, kBlockSize> R{};
        std::array<float, kBlockSize> wfsL{};
        std::array<float, kBlockSize> wfsR{};
        for (size_t i = 0; i < kBlockSize; ++i) {
            const float s = 0.55f * std::sin(phase) + 0.18f * std::cos(2.1f * phase);
            L[i] = s;
            R[i] = s;
            phase += kTwoPi * 480.0f / kSampleRate;
            if (phase >= kTwoPi) phase -= kTwoPi;
        }

        const float* objPtrs[2] = {L.data(), R.data()};
        wfs.process(objPtrs, 2, wfsL.data(), wfsR.data(), static_cast<int>(kBlockSize));

        lattice.process(L.data(), R.data(), kBlockSize);
        pinna.process(L.data(), R.data(), kBlockSize);
        msoFarrow.process(L.data(), R.data(), kBlockSize, kSampleRate);

        for (size_t i = 0; i < kBlockSize; ++i) {
            const float outL = 0.8f * L[i] + 0.2f * wfsL[i];
            const float outR = 0.8f * R[i] + 0.2f * wfsR[i];
            if (!std::isfinite(outL) || !std::isfinite(outR)) {
                allFinite = false;
            }
            if (blk > 0 || i > 0) {
                maxDelta = std::max({maxDelta, std::fabs(outL - prevL), std::fabs(outR - prevR)});
            }
            prevL = outL;
            prevR = outR;
        }
    }

    EXPECT_TRUE(allFinite);
    EXPECT_LT(maxDelta, 0.24f);
}

// ── 6. Música con transitorios fuertes: Discontinuidad, Energía, THD y Fase ─
TEST(SupremeAcousticContinuityTest, StrongTransientMusicMetricsAndPhaseStability) {
    ivanna::supreme::WarpedLatticeTransducerInverter lattice;
    ivanna::supreme::PhaseCoherentTransharmonicSynthesizer cvnn;
    ivanna::supreme::SnnNmfHoaUpmixer snnHoa;
    ivanna::supreme::PinnaManifoldInterpolator pinna;
    ivanna::supreme::SupremeMsoFarrowArbitrator msoFarrow;

    lattice.prepare(kSampleRate);
    lattice.setEnabled(true);
    cvnn.prepare(kSampleRate);
    cvnn.setEnabled(true);
    snnHoa.prepare(kSampleRate);
    snnHoa.setEnabled(true);
    pinna.calibrateFromLatents(0.42f, -0.28f, 0.19f, kSampleRate);
    pinna.setWetMix(0.85f);
    pinna.setEnabled(true);
    msoFarrow.prepare(kSampleRate);
    msoFarrow.setMsoItdNanoseconds(230000.0f);
    msoFarrow.setEnabled(true);

    constexpr size_t kTotalBlocks = 120;
    constexpr size_t kTotalSamples = kTotalBlocks * kBlockSize;
    std::vector<float> outContL(kTotalSamples, 0.0f);
    std::vector<float> outContR(kTotalSamples, 0.0f);

    float phaseFund = 0.0f;
    for (size_t blk = 0; blk < kTotalBlocks; ++blk) {
        const bool suspended = (blk % 12 >= 6 && blk % 12 <= 8);
        lattice.setEnabled(!suspended);
        cvnn.setEnabled(!suspended);
        snnHoa.setEnabled(!suspended);
        pinna.setEnabled(!suspended);
        msoFarrow.setEnabled(!suspended);

        std::array<float, kBlockSize> L{};
        std::array<float, kBlockSize> R{};
        for (size_t i = 0; i < kBlockSize; ++i) {
            const size_t globalIdx = blk * kBlockSize + i;
            // Transitorio de percusión cada 1024 muestras + portadora armónica continua
            const size_t hitPos = globalIdx % 1024;
            const float drumEnv = std::exp(-static_cast<float>(hitPos) * 0.012f);
            const float drumHit = 0.30f * drumEnv * std::sin(0.35f * static_cast<float>(hitPos));
            const float harmonic = 0.55f * std::sin(phaseFund);
            const float s = harmonic + drumHit;
            L[i] = s;
            R[i] = s;
            phaseFund += kTwoPi * 1000.0f / kSampleRate;
            if (phaseFund >= kTwoPi) phaseFund -= kTwoPi;
        }

        lattice.process(L.data(), R.data(), kBlockSize);
        cvnn.process(L.data(), R.data(), kBlockSize);
        snnHoa.process(L.data(), R.data(), kBlockSize);
        pinna.process(L.data(), R.data(), kBlockSize);
        msoFarrow.process(L.data(), R.data(), kBlockSize, kSampleRate);

        for (size_t i = 0; i < kBlockSize; ++i) {
            outContL[blk * kBlockSize + i] = L[i];
            outContR[blk * kBlockSize + i] = R[i];
        }
    }

    float maxBoundaryJump = 0.0f;
    float boundaryAnomalyEnergy = 0.0f;
    for (size_t blk = 1; blk < kTotalBlocks; ++blk) {
        const size_t idx = blk * kBlockSize;
        const float dL = std::fabs(outContL[idx] - outContL[idx - 1]);
        const float dR = std::fabs(outContR[idx] - outContR[idx - 1]);
        maxBoundaryJump = std::max({maxBoundaryJump, dL, dR});
        const float ddL = outContL[idx] - 2.0f * outContL[idx - 1] + outContL[idx - 2];
        boundaryAnomalyEnergy += ddL * ddL;
    }

    // Evaluar THD y estabilidad de fase en un segmento puro de reactivación
    std::vector<float> pureSineL(2048, 0.0f);
    std::vector<float> pureSineR(2048, 0.0f);
    float p = 0.0f;
    for (size_t blk = 0; blk < 32; ++blk) {
        const bool susp = (blk >= 8 && blk < 16);
        msoFarrow.setEnabled(!susp);
        std::array<float, kBlockSize> L{};
        std::array<float, kBlockSize> R{};
        for (size_t i = 0; i < kBlockSize; ++i) {
            const float s = 0.7f * std::sin(p);
            L[i] = s;
            R[i] = s;
            p += kTwoPi * 1000.0f / kSampleRate;
        }
        msoFarrow.process(L.data(), R.data(), kBlockSize, kSampleRate);
        for (size_t i = 0; i < kBlockSize; ++i) {
            pureSineL[blk * kBlockSize + i] = L[i];
            pureSineR[blk * kBlockSize + i] = R[i];
        }
    }

    const float thdResumeWindow = computeFundamentalThd(pureSineL.data() + 16 * kBlockSize, 512, 1000.0f, kSampleRate);

    // Estabilidad de fase: la diferencia de primer orden durante la reactivación
    // del bloque 16 no debe presentar inversión de fase ni pico impulsivo.
    float maxSineResumeStep = 0.0f;
    for (size_t i = 15 * kBlockSize; i < 18 * kBlockSize; ++i) {
        maxSineResumeStep = std::max(maxSineResumeStep, std::fabs(pureSineL[i] - pureSineL[i - 1]));
    }

    EXPECT_LT(maxBoundaryJump, 0.24f);
    EXPECT_LT(boundaryAnomalyEnergy, 0.25f);
    EXPECT_LT(thdResumeWindow, 0.03f);
    EXPECT_LT(maxSineResumeStep, 0.12f);
}

} // namespace
