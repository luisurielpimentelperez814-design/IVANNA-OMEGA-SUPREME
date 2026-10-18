// test_spatial_width_readback.cpp — puerta host del contrato de read-back del ancho espacial.
// (c) 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// Cierra un hueco real: el read-back de Fotoholografía (nativeGetSpatialWidthDirect) vive en
// ivanna_omega_jni.cpp, que NO se compila en host. La UI (FotoHoloEngine.readbackConfirmed)
// decide si el motor "confirmó" con `dspWidth <= appliedWidth + 0.01`. Sin prueba, ese
// contrato era una suposición. Aquí se verifica la función que el JNI usa de verdad.
#include <gtest/gtest.h>

#include "supreme/SpatialWidthReadback.hpp"

#include <cmath>
#include <limits>

using ivanna::supreme::SpatialWidthReadback;

TEST(SpatialWidthReadback, NominalThermalScaleIsIdentity) {
    // Sin recorte térmico (escala = 1) el ancho entregado es exactamente el pedido.
    EXPECT_FLOAT_EQ(SpatialWidthReadback::applied(1.32f, 1.0f), 1.32f);
    EXPECT_FLOAT_EQ(SpatialWidthReadback::applied(0.0f, 1.0f), 0.0f);
    EXPECT_FLOAT_EQ(SpatialWidthReadback::applied(2.0f, 1.0f), 2.0f);
}

TEST(SpatialWidthReadback, ThermalScaleOnlyReduces) {
    // El contrato del que depende la UI: con escala <= 1 el ancho aplicado NUNCA supera la base.
    const float base = 1.32f;
    for (float s = 0.0f; s <= 1.0f; s += 0.05f) {
        const float a = SpatialWidthReadback::applied(base, s);
        EXPECT_LE(a, base + 1e-6f) << "escala " << s;
        EXPECT_GE(a, 0.0f);
    }
}

TEST(SpatialWidthReadback, HalvedThermalScaleHalvesWidth) {
    EXPECT_NEAR(SpatialWidthReadback::applied(1.32f, 0.5f), 0.66f, 1e-6f);
}

TEST(SpatialWidthReadback, ZeroThermalScaleIsSilentWidth) {
    EXPECT_FLOAT_EQ(SpatialWidthReadback::applied(1.32f, 0.0f), 0.0f);
}

TEST(SpatialWidthReadback, ClampsToEngineRange) {
    // El parámetro del StereoWidener vive en [0, 2]; fuera de ahí se acota, no se propaga.
    EXPECT_FLOAT_EQ(SpatialWidthReadback::applied(5.0f, 1.0f), 2.0f);
    EXPECT_FLOAT_EQ(SpatialWidthReadback::applied(-3.0f, 1.0f), 0.0f);
    EXPECT_FLOAT_EQ(SpatialWidthReadback::applied(2.0f, 3.0f), 2.0f); // escala >1 acotada a 1
}

TEST(SpatialWidthReadback, NonFiniteInputsAreSafeAndFinite) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    EXPECT_TRUE(std::isfinite(SpatialWidthReadback::applied(nan, 1.0f)));
    EXPECT_TRUE(std::isfinite(SpatialWidthReadback::applied(1.32f, nan)));
    EXPECT_TRUE(std::isfinite(SpatialWidthReadback::applied(inf, nan)));
    EXPECT_TRUE(std::isfinite(SpatialWidthReadback::applied(-inf, inf)));
    // Base no finita -> valor seguro por defecto; escala no finita -> sin recorte (1.0).
    EXPECT_FLOAT_EQ(SpatialWidthReadback::applied(nan, 1.0f), 1.32f);
    EXPECT_FLOAT_EQ(SpatialWidthReadback::applied(1.0f, nan), 1.0f);
}

TEST(SpatialWidthReadback, MonotonicNonDecreasingInThermalScale) {
    // Más escala térmica no puede dar menos ancho: sin histéresis invertida.
    float prev = -1.0f;
    for (float s = 0.0f; s <= 1.0f; s += 0.02f) {
        const float a = SpatialWidthReadback::applied(1.32f, s);
        EXPECT_GE(a + 1e-6f, prev) << "escala " << s;
        prev = a;
    }
}

TEST(SpatialWidthReadback, ReadbackConfirmedContractHolds) {
    // Simula el cálculo de FotoHoloEngine.readbackConfirmed: el read-back debe ser coherente
    // con el último comando para cualquier recorte térmico realista.
    const float commanded = 1.32f;         // lo que la UI pidió
    for (float s = 0.0f; s <= 1.0f; s += 0.1f) {
        const float dspWidth = SpatialWidthReadback::applied(commanded, s);
        const bool confirmed = std::isfinite(dspWidth) && dspWidth <= commanded + 0.01f;
        EXPECT_TRUE(confirmed) << "escala " << s << " dspWidth " << dspWidth;
    }
}
