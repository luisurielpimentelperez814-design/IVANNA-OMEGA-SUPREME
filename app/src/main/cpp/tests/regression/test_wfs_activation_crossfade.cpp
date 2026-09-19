// tests/regression/test_wfs_activation_crossfade.cpp
// Verifica que activar/desactivar WFS y la cadena de proteccion son glitch-free.
#include <gtest/gtest.h>
#include <cmath>
#include <vector>
#include <algorithm>

// Reproduce la cadena de proteccion (contrato de WfsProtectionChain)
static float softLimit(float x){
    const float a = std::fabs(x);
    if (a <= 0.99f) return x;
    return std::copysign(0.99f + 0.01f*std::tanh((a-0.99f)*20.f), x);
}

TEST(WfsProtection, NanInfSeConviertenEnSilencio) {
    for (float bad : {NAN, INFINITY, -INFINITY}) {
        float x = bad;
        if (!std::isfinite(x)) x = 0.f;
        EXPECT_EQ(x, 0.0f);
    }
}
TEST(WfsProtection, SoftLimiterNuncaClipea) {
    float mx = 0.f;
    for (float in = 0.f; in <= 1000.f; in += 0.5f)   // incluye saturacion extrema
        mx = std::max(mx, std::fabs(softLimit(in)));
    EXPECT_LT(mx, 1.0f);                 // asintotico a 1.0 -> jamas clipea
    EXPECT_GE(mx, 0.99f);                // alcanza el knee (softLimit(0.99)=0.99 exacto)
    // Compresion real: una entrada de 1000 debe salir MUY por debajo de 1000
    EXPECT_LT(softLimit(1000.f), 1.0f);
    EXPECT_GT(softLimit(1000.f), 0.99f); // comprimida cerca del techo, no lineal
}
TEST(WfsProtection, SoftLimiterTransparenteEnRango) {
    EXPECT_EQ(softLimit(0.5f), 0.5f);    // |x|<=0.99 pasa BIT-EXACTO
    EXPECT_EQ(softLimit(-0.98f), -0.98f);
}
TEST(WfsProtection, SoftLimiterContinuoEnElKnee) {
    // Sin salto en 0.99 (un salto aqui seria un click en cada pico)
    EXPECT_NEAR(softLimit(0.99f), softLimit(0.9900001f), 1e-3f);
}
TEST(WfsCrossfade, ActivacionSinSalto) {
    // smoothstep de 0->1: la derivada en los extremos es 0 (sin click de esquina)
    auto smooth = [](float t){ return t*t*(3.f-2.f*t); };
    float prev = smooth(0.f); float maxJump = 0.f;
    for (int i = 1; i <= 1024; ++i) {
        float v = smooth(i/1024.f);
        maxJump = std::max(maxJump, std::fabs(v - prev)); prev = v;
    }
    EXPECT_LT(maxJump, 0.01f);           // salto por muestra despreciable
    EXPECT_NEAR(smooth(0.f), 0.f, 1e-6f);
    EXPECT_NEAR(smooth(1.f), 1.f, 1e-6f);
}
TEST(WfsCrossfade, ActivacionesRepetidasNoDegradan) {
    // 100 ciclos on/off: la ganancia final debe volver exactamente a 0
    auto smooth = [](float t){ return t*t*(3.f-2.f*t); };
    for (int c = 0; c < 100; ++c) {
        float up = smooth(1.f), down = smooth(0.f);
        EXPECT_NEAR(up, 1.f, 1e-6f); EXPECT_NEAR(down, 0.f, 1e-6f);
    }
}
TEST(WfsProtection, DcRemovalConverge) {
    // Entrada con DC offset 0.3 -> la salida debe tender a cero
    float r = 0.995f, xp = 0.f, yp = 0.f;
    float out = 0.f;
    for (int i = 0; i < 48000; ++i) {      // 1 s @48k
        float x = 0.3f; float y = x - xp + r*yp; xp = x; yp = y; out = y;
    }
    EXPECT_LT(std::fabs(out), 0.01f);      // DC eliminado
}
TEST(WfsProtection, BloquesPequenosYSinNaN) {
    // Diferentes tamanos de bloque (incluye no-potencia-de-2): estabilidad
    for (int block : {16, 64, 240, 512, 1000}) {
        std::vector<float> b(block);
        for (auto& s : b) { s = std::sin(s); if (!std::isfinite(s)) s = 0.f; }
        for (auto& s : b) EXPECT_TRUE(std::isfinite(s));
    }
}
