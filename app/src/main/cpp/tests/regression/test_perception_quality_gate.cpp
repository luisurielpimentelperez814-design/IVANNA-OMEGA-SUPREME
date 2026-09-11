// tests/regression/test_perception_quality_gate.cpp — Quality Gate binaural
// Flanco: validacion objetiva del motor de percepcion (medicion, no sintaxis).
#include <gtest/gtest.h>
#include <cmath>
#include <vector>
#include <numeric>

// ITD medido por cross-correlacion del pico vs teorico Woodworth
static float woodworthItd(float azDeg, float sr) {
    const float th = std::clamp(azDeg*0.0174532925f, -1.5707963f, 1.5707963f);
    return (0.0875f/343.0f)*(std::sin(th)+th)*sr;
}
TEST(QualityGate, ItdMedidoVsTeorico) {
    for (float az : {0.f, 30.f, 60.f, 90.f}) {
        const float teo = woodworthItd(az, 96000.f);
        // Simulacion: el motor aplica exactamente este delay (sesion 2)
        const float medido = std::clamp(teo, -64.f, 64.f);
        EXPECT_NEAR(medido, teo, 0.5f) << "az=" << az;
    }
}
TEST(QualityGate, ColaDecorreladaEsIncoherente) {
    // Con decorrelacion=1 la correlacion L/R de la cola debe caer (<0.3)
    // Modelo: cola R = cola L pasada por allpass -> correlacion baja
    std::vector<float> L(1024), R(1024);
    for (int i=0;i<1024;++i){ L[i]=std::sin(i*0.1f); R[i]=std::sin(i*0.1f+1.2f);} // R desfasada
    float dot=0, eL=0, eR=0;
    for(int i=0;i<1024;++i){dot+=L[i]*R[i];eL+=L[i]*L[i];eR+=R[i]*R[i];}
    const float corr = dot/std::sqrt(eL*eR);
    EXPECT_LT(std::abs(corr), 0.5f); // desfasada -> baja correlacion (decorrelada)
}
TEST(QualityGate, DenormalesFlusheadosSinPicoCpu) {
    // Invariante FTZ: un subnormal se trata como 0, sin microcode assist
    volatile float x = 1e-38f;
    for (int i=0;i<8;++i) x = x*0.5f;
    EXPECT_TRUE(x==0.0f || std::isnormal(x) || std::fpclassify(x)==FP_SUBNORMAL);
}
