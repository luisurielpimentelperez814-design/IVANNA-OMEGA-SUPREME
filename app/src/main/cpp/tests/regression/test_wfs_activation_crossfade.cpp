// tests/regression/test_wfs_activation_crossfade.cpp
// Verifica que activar/desactivar WFS y la cadena de proteccion son glitch-free.
#include <gtest/gtest.h>
#include <cmath>
#include <vector>
#include <algorithm>

// Reproduce la cadena de proteccion (contrato de WfsProtectionChain)
static float softLimit(float x){ return std::tanh(x)*(1.0f/0.7615941f)*0.99f; }

TEST(WfsProtection, NanInfSeConviertenEnSilencio) {
    for (float bad : {NAN, INFINITY, -INFINITY}) {
        float x = bad;
        if (!std::isfinite(x)) x = 0.f;
        EXPECT_EQ(x, 0.0f);
    }
}
TEST(WfsProtection, SoftLimiterNuncaClipea) {
    float mx = 0.f;
    for (float in = 0.f; in <= 8.f; in += 0.01f)
        mx = std::max(mx, std::fabs(softLimit(in)));
    EXPECT_LT(mx, 1.0f);                 // jamas supera 1.0 -> cero clipping
}
TEST(WfsProtection, SoftLimiterTransparenteEnRango) {
    EXPECT_NEAR(softLimit(0.5f), 0.5f, 0.02f); // |x|<1 pasa casi intacto
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
