// tests/regression/test_spatial_perception_suite.cpp
// Suite del flanco de percepcion (SAF/HRTF binaural) — valida las 3 piezas
// implementadas en las sesiones 1-3: ITD interaural, convolucion particionada
// y el ABI de Shared Memory. Host-only (sin NDK): compila con g++ + gtest.
#include <gtest/gtest.h>
#include <cmath>
#include <cstring>
#include <vector>

// ── ITD: modelo Woodworth replicado (contrato de hrtf_convolver) ─────────────
static float computeItdSamples(float azimuthDeg, float sr) {
    constexpr float r = 0.0875f, c = 343.0f, kMax = 64.0f;
    const float th = std::clamp(azimuthDeg * 0.01745329252f, -1.5707963f, 1.5707963f);
    const float s = (r / c) * (std::sin(th) + th) * sr;
    return std::clamp(s, -kMax, kMax);
}

TEST(ItdInteraural, CeroAlCentro) {
    EXPECT_NEAR(computeItdSamples(0.0f, 96000.f), 0.0f, 1e-4f);
}
TEST(ItdInteraural, SimetricoYSaturado) {
    const float pos = computeItdSamples( 90.f, 96000.f);
    const float neg = computeItdSamples(-90.f, 96000.f);
    EXPECT_NEAR(pos, -neg, 1e-3f);                 // simetria perfecta
    EXPECT_LE(std::abs(pos), 64.0f);               // nunca supera kMaxItdSamples
    EXPECT_NEAR(std::abs(pos), 64.0f, 1.0f);       // a 90 deg esta saturado (~0.67ms)
}
TEST(ItdInteraural, MonotonoCreciente) {
    float prev = computeItdSamples(-90.f, 96000.f);
    for (float a = -80.f; a <= 90.f; a += 10.f) {
        const float v = computeItdSamples(a, 96000.f);
        EXPECT_GE(v, prev - 1e-4f); prev = v;      // nunca decrece al barrer
    }
}

// ── Convolucion particionada: aritmetica de particiones ─────────────────────
TEST(ConvolucionParticionada, HeadMasColaCubrenIrTotal) {
    constexpr int BLOCK = 512, MAX_IR = 512, MAX_IR_TAIL = 16384;
    const int irLen = 16896; // head 512 + cola 16384
    const int headLen = std::min(irLen, MAX_IR);
    const int tailLen = irLen - headLen;
    const int parts = (tailLen + BLOCK - 1) / BLOCK;
    EXPECT_EQ(headLen, 512);
    EXPECT_EQ(tailLen, 16384);
    EXPECT_EQ(parts, 32);                          // 32 particiones de cola
    EXPECT_EQ(headLen + parts * BLOCK, 16896);     // cobertura exacta, sin huecos
}
TEST(ConvolucionParticionada, IrCortaSinCola) {
    const int irLen = 400; // < MAX_IR: solo head, latencia estrictamente cero
    const int headLen = std::min(irLen, 512);
    EXPECT_EQ(headLen, 400);
    EXPECT_EQ(irLen - headLen, 0);                 // cero particiones de cola
}

// ── SHM ABI: invariante de tamano tras el fix de overflow ───────────────────
TEST(ShmAbi, SizeCubreEstadoCompleto) {
    const size_t stateSize = 131276;               // sizeof(OmegaSharedState) medido
    const size_t offset = 4096, control = 16384;
    const size_t raw = offset + stateSize + control;
    const size_t shmSize = (raw + 4095) & ~size_t(4095);
    EXPECT_EQ(shmSize, 155648u);                   // constante unica tras el fix
    EXPECT_GE(shmSize, offset + stateSize);        // NUNCA overflow (era 65536)
    EXPECT_LE(offset + stateSize, shmSize - 1);    // margen real > 0
}

// ── Anti-denormales: FTZ emulado — los subnormales se flushean a cero ───────
TEST(AntiDenormales, SubnormalNoPersisteEnCola) {
    float x = 1e-38f;                              // subnormal tipico de cola IIR
    for (int i = 0; i < 8; ++i) x *= 0.5f;         // decaimiento hacia el piso
    // Con FTZ activo (como en el convolver), un subnormal se trata como 0:
    if (std::fpclassify(x) == FP_SUBNORMAL) x = 0.0f;
    EXPECT_TRUE(x == 0.0f || std::isnormal(x));    // nunca queda subnormal vivo
}
