#include <gtest/gtest.h>
#include "../spatial/HoaBinauralDecoder.hpp"
#include <cmath>

using namespace Ivanna;

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
