#include <gtest/gtest.h>
#include "../spatial/HrtfPersonalizer.hpp"
#include "../spatial/ObjectSpatialRenderer.hpp"
#include "../spatial/StereoObjectDecomposer.hpp"
#include "../spatial/IvannaAudioPipeline.hpp"
#include <cmath>
#include <vector>

using namespace ivanna::spatial;

// Eje 2 — auditoria 2026-09-24: canal_resonance_boost_db estaba declarado en
// UserPinnaProfile y calculado hacia ningun sitio (recalculate() lo ignoraba
// por completo). Este test falla si alguien vuelve a dejarlo muerto: un
// boost positivo debe aumentar la energia de la banda alrededor del notch
// de pinna frente a boost 0 dB, para la MISMA senal de entrada.
TEST(HrtfPersonalizerWiring, CanalResonanceBoostChangesOutputEnergy) {
    constexpr size_t kBlock = 256;
    std::vector<float> flat(kBlock, 0.0f);
    std::vector<float> boosted(kBlock, 0.0f);
    for (size_t i = 0; i < kBlock; ++i) {
        // Ruido determinista de banda ancha (suma de senos) para excitar el
        // notch de pinna en vez de un tono puro.
        float t = static_cast<float>(i) / 48000.0f;
        float s = 0.3f * std::sin(2.0f * 3.14159265f * 3000.0f * t) +
                  0.3f * std::sin(2.0f * 3.14159265f * 7000.0f * t);
        flat[i] = s;
        boosted[i] = s;
    }

    UserPinnaProfile flatProfile;
    flatProfile.canal_resonance_boost_db = 0.0f;
    HrtfPersonalizer flatP;
    flatP.setProfile(flatProfile);
    flatP.processChannel(flat.data(), kBlock);

    UserPinnaProfile boostProfile;
    boostProfile.canal_resonance_boost_db = 9.0f;
    HrtfPersonalizer boostP;
    boostP.setProfile(boostProfile);
    boostP.processChannel(boosted.data(), kBlock);

    double energyFlat = 0.0, energyBoosted = 0.0;
    for (size_t i = 0; i < kBlock; ++i) {
        energyFlat += flat[i] * flat[i];
        energyBoosted += boosted[i] * boosted[i];
    }
    EXPECT_GT(energyBoosted, energyFlat)
        << "canal_resonance_boost_db=9dB debe producir mas energia que 0dB "
           "para la misma entrada — si esto falla, el campo volvio a quedar "
           "sin efecto real en processChannel().";
}

// El clamp +-12dB debe seguir siendo finito y estable incluso ante un
// perfil fuera de rango (defensa contra un futuro caller descuidado).
TEST(HrtfPersonalizerWiring, ResonanceBoostClampStaysFiniteOutOfRange) {
    UserPinnaProfile p;
    p.canal_resonance_boost_db = 500.0f; // fuera de rango a proposito
    HrtfPersonalizer hp;
    hp.setProfile(p);

    std::vector<float> buf(64, 1.0f);
    hp.processChannel(buf.data(), buf.size());
    for (float v : buf) {
        EXPECT_TRUE(std::isfinite(v));
    }
}

// Eje 2/4 — auditoria 2026-09-24: getItdScale() no tenia ningun caller en
// todo el arbol (confirmado por grep). Ahora ObjectSpatialRenderer lo
// consume para aplicar un ITD real (retardo interaural dependiente del
// angulo, no solo ganancia). Este test verifica que un objeto panorizado
// hacia un lado produce un retardo medible en el canal opuesto cuando
// itdScale > 0, y ningun retardo cuando itdScale == 0 (comportamiento
// previo, para no romper nada que dependa de el).
TEST(ObjectSpatialRendererWiring, PannedObjectProducesInterauralDelayWithItdScale) {
    constexpr size_t kBlock = 128;
    StereoObjectDecomposer decomposer;
    // Senal fuertemente lateralizada (L >> R) para que la energia caiga en
    // el objeto LEFT (x negativo), que si tiene pan real.
    std::vector<float> inL(kBlock, 0.0f);
    std::vector<float> inR(kBlock, 0.0f);
    inL[0] = 1.0f; // impulso solo en L: mid=side=0.5, fuerza objeto lateral

    float* objBuf[4];
    std::vector<std::vector<float>> objStorage(4, std::vector<float>(kBlock, 0.0f));
    for (int i = 0; i < 4; ++i) objBuf[i] = objStorage[i].data();
    decomposer.decompose(inL.data(), inR.data(), objBuf, kBlock);

    ObjectSpatialRenderer rendererNoItd;
    ObjectSpatialRenderer rendererWithItd;
    std::vector<float> outL0(kBlock, 0.0f), outR0(kBlock, 0.0f);
    std::vector<float> outL1(kBlock, 0.0f), outR1(kBlock, 0.0f);

    const float* constObjBuf[4] = {objBuf[0], objBuf[1], objBuf[2], objBuf[3]};
    rendererNoItd.renderObjects(constObjBuf, decomposer.getObjects(),
                                 outL0.data(), outR0.data(), kBlock, 0.0f);
    rendererWithItd.renderObjects(constObjBuf, decomposer.getObjects(),
                                   outL1.data(), outR1.data(), kBlock, 1.0f);

    // Con itdScale=1 debe haber una diferencia real frente a itdScale=0 en
    // al menos una de las dos salidas (el retardo interaural desplaza
    // energia entre L/R en el tiempo).
    bool anyDifference = false;
    for (size_t i = 0; i < kBlock; ++i) {
        if (std::fabs(outL1[i] - outL0[i]) > 1e-6f ||
            std::fabs(outR1[i] - outR0[i]) > 1e-6f) {
            anyDifference = true;
            break;
        }
    }
    EXPECT_TRUE(anyDifference)
        << "itdScale != 0 no cambio la salida — getItdScale() volvio a "
           "quedar sin consumidor real.";

    for (size_t i = 0; i < kBlock; ++i) {
        EXPECT_TRUE(std::isfinite(outL1[i]));
        EXPECT_TRUE(std::isfinite(outR1[i]));
    }
}

TEST(ObjectSpatialRendererWiring, LivePipelineActiveInstanceWiringAndChunkedAxes) {
    IvannaAudioPipeline livePipe;
    livePipe.prepare(48000.0f, IvannaAudioPipeline::MAX_BLOCK_SIZE);
    IvannaAudioPipeline::setActiveInstance(&livePipe);
    EXPECT_EQ(IvannaAudioPipeline::peekActiveInstance(), &livePipe);
    EXPECT_EQ(&IvannaAudioPipeline::getActiveInstance(), &livePipe);

    // Control Ejes 2, 3, 5, 6 through getActiveInstance()
    UserPinnaProfile pinna{};
    pinna.head_circumference_cm = 60.0f;
    pinna.ear_pinna_size_mm = 70.0f;
    pinna.canal_resonance_boost_db = 2.5f;
    IvannaAudioPipeline::getActiveInstance().personalizer().setProfile(pinna);
    IvannaAudioPipeline::getActiveInstance().roomEngine().setInversionGain(0.35f);
    IvannaAudioPipeline::getActiveInstance().roomEngine().setProjectionWet(0.22f);
    IvannaAudioPipeline::getActiveInstance().physicalScene().setOcclusion(0.15f);
    IvannaAudioPipeline::getActiveInstance().physicalScene().setWallAbsorption(0.42f);
    IvannaAudioPipeline::getActiveInstance().hearingEngine().setListeningSpl(62.0f);

    // Process a 1024-sample block (> MAX_BLOCK_SIZE) via processLiveSpatialAxes
    constexpr size_t kLargeBlock = 1024;
    std::vector<float> bufL(kLargeBlock), bufR(kLargeBlock);
    for (size_t i = 0; i < kLargeBlock; ++i) {
        bufL[i] = 0.45f * std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / 48000.0f);
        bufR[i] = 0.35f * std::cos(2.0f * 3.14159265f * 660.0f * static_cast<float>(i) / 48000.0f);
    }
    IvannaAudioPipeline::getActiveInstance().processLiveSpatialAxes(
        bufL.data(), bufR.data(), kLargeBlock, 48000.0f, true, 0.18f, true);

    double energy = 0.0;
    for (size_t i = 0; i < kLargeBlock; ++i) {
        EXPECT_TRUE(std::isfinite(bufL[i]));
        EXPECT_TRUE(std::isfinite(bufR[i]));
        energy += static_cast<double>(bufL[i]) * bufL[i] + static_cast<double>(bufR[i]) * bufR[i];
    }
    EXPECT_GT(energy, 1.0e-3);

    IvannaAudioPipeline::setActiveInstance(nullptr);
    EXPECT_EQ(IvannaAudioPipeline::peekActiveInstance(), nullptr);
}

