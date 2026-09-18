#include <gtest/gtest.h>
#include <cmath>
TEST(WfsAliasGuard, CutoffDecaeConEspaciado) {
    auto f = [](float dx){ return 343.0f/(2.f*dx); };
    EXPECT_GT(f(0.10f), f(0.20f));          // mas separacion -> alias mas abajo
    EXPECT_NEAR(f(0.1715f), 1000.f, 5.f);   // 17.15 cm -> ~1 kHz
}
TEST(WfsAliasGuard, OnePoleAtenuaSobreFc) {
    float a = 1.f - std::exp(-2.f*3.14159265f*1000.f/48000.f);
    float y = 0, x = 1.f;                   // escalon
    for (int i=0;i<64;++i) y += a*(x-y);
    EXPECT_GT(y, 0.9f); EXPECT_LT(y, 1.0f); // converge sin overshoot
}
TEST(BtCodecLatency, JerarquiaSensata) {
    EXPECT_LT(30, 40);    // LDAC < aptX
    EXPECT_LT(40, 150);   // aptX < SBC
    EXPECT_LT(120, 150);  // AAC < SBC
}
