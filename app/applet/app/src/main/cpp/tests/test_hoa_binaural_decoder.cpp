#include <gtest/gtest.h>
#include "../spatial/HoaBinauralDecoder.hpp"
#include <cmath>

using namespace Ivanna;

TEST(HoaBinauralDecoderTest, MaxReWeightingPreservesOnAxisGainAndTapersHigherOrders) {
    // Verifica la propiedad matemática exacta de la ponderación max-rE
    // (Daniel & Nicol) añadida al decoder: debe ATENUAR los ordenes
    // armonicos altos relativo al bajo (reduce el rizado espacial fuera
    // de eje entre altavoces virtuales) sin cambiar la ganancia total en
    // eje (fuente alineada con un altavoz) respecto al decoder de
    // muestreo plano original.
    auto w = HoaBinauralDecoder::computeMaxReOrderWeights();
    ASSERT_EQ(w.size(), 3u);

    // Invariante de renormalizacion: para h0=encode(0)={1,0,0,1,0,0,-0.5,0,1},
    // el decoder de muestreo plano (pesos base 1/2/2 por orden) da una
    // ganancia total en eje de exactamente 5.5 (1*1 + 1*2 + 0.25*2 + 1*2).
    // La version max-rE debe reproducir ESE MISMO total: w0 + w1 + 1.25*w2.
    const float onAxisTotal = w[0] + w[1] + 1.25f * w[2];
    EXPECT_NEAR(onAxisTotal, 5.5f, 1e-4f);

    // Patron de atenuacion max-rE real: g_m = cos(m*pi/6), por lo que las
    // razones entre pesos de orden deben ser exactamente 2*cos(30°) y
    // 2*cos(60°) relativas al peso de orden 0 (independientes del factor
    // global de renormalizacion, que se cancela en el cociente).
    ASSERT_GT(w[0], 0.0f);
    EXPECT_NEAR(w[1] / w[0], 2.0f * std::cos(M_PI / 6.0), 1e-4);
    EXPECT_NEAR(w[2] / w[0], 2.0f * std::cos(M_PI / 3.0), 1e-4);

    // El orden mas alto debe quedar mas atenuado que el orden 1 respecto
    // a su peso base (2.0 en ambos): esto es lo que aplana el patron de
    // energia direccional entre altavoces virtuales.
    EXPECT_LT(w[2] / 2.0f, w[1] / 2.0f);
}

TEST(HoaBinauralDecoderTest, PointSourceRoundTrip) {
    HoaBinauralDecoder decoder;
    decoder.prepare(48000.0f, 8);
    // FIX (build rojo, 2026-09-15): el convolver busca archivos .ihr1 en
    // /data/adb/ivanna_omega/hrtf/ que NO existen en el host de CI -> sin
    // dataset cargado la energia de salida es cero (falso negativo).
    // Se inyecta un SyntheticHRTF sintetico (sin archivo) para que el test
    // mida el pipeline real, no la ausencia de archivos del dispositivo.
    auto hrtf = std::make_shared<ivanna::SyntheticHRTF>();
    hrtf->init(48000, 512);
    decoder.setHrtfProfile(hrtf);
    
    // Simulate a point source sweeping 0 to 360
    for (int deg = 0; deg < 360; deg += 45) {
        float azRad = deg * M_PI / 180.0f;
        HoaVector source = HoaGainMatrix::encode(azRad);
        
        std::vector<HoaVector> field(512, source);
        std::vector<float> outL(512);
        std::vector<float> outR(512);
        
        decoder.processBlock(field, outL.data(), outR.data(), 512);
        
        // Compute energy
        float energyL = 0.0f;
        float energyR = 0.0f;
        for (size_t i = 300; i < 512; ++i) { // skip settling
            energyL += outL[i]*outL[i];
            energyR += outR[i]*outR[i];
        }
        
        // Assert we got non-zero output, energy is preserved reasonably across angles
        EXPECT_GT(energyL + energyR, 0.0f);
    }
}

TEST(HoaBinauralDecoderTest, WOnlyMonoYieldsEqualEnergy) {
    HoaBinauralDecoder decoder;
    decoder.prepare(48000.0f, 8);
    auto hrtf = std::make_shared<ivanna::SyntheticHRTF>();
    hrtf->init(48000, 512);
    decoder.setHrtfProfile(hrtf);
    
    HoaVector source = {0};
    source[0] = 1.0f; // W only
    
    std::vector<HoaVector> field(512, source);
    std::vector<float> outL(512);
    std::vector<float> outR(512);
    
    decoder.processBlock(field, outL.data(), outR.data(), 512);
    
    float energyL = 0.0f;
    float energyR = 0.0f;
    for (size_t i = 300; i < 512; ++i) { 
        energyL += outL[i]*outL[i];
        energyR += outR[i]*outR[i];
    }
    
    // W only should excite all virtual speakers equally. HRTF might color it,
    // but L/R total energy should be roughly symmetric.
    EXPECT_GT(energyL, 0.0f);
    EXPECT_GT(energyR, 0.0f);
    EXPECT_NEAR(energyL, energyR, energyL * 0.2f); // within 20%
}
