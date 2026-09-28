// Tests reales para HoaGainMatrix::encode (ver spatial/HoaGainMatrix.hpp para
// la justificación matemática completa). Cada aserción compara contra un
// valor EXACTO conocido de las identidades trigonométricas usadas — no
// tolerancias arbitrarias para "que pase".

#include <gtest/gtest.h>
#include <cmath>
#include "../spatial/HoaGainMatrix.hpp"

// FIX (build rojo, 2026-09-15): el header HoaGainMatrix.hpp declara
// `namespace Ivanna` (I mayúscula, commits d8c1af8/fff60f1) pero este test
// seguía usando `ivanna::` minúscula -> "'ivanna' has not been declared".
// Se actualiza el test al namespace nuevo; el header NO se toca (es el
// cambio nuevo que hay que preservar).
using Ivanna::HoaGainMatrix;
using Ivanna::HoaVector;

namespace {
constexpr float kEps = 1e-5f;
constexpr float kPi = 3.14159265358979323846f;
}  // namespace

TEST(HoaGainMatrix, FrenteAzimuthCero) {
    // az=0: cos=1, sin=0 -> X=1, Y=0, U=cos(0)=1, V=sin(0)=0
    HoaVector v = HoaGainMatrix::encode(0.0f);
    EXPECT_NEAR(v[0], 1.0f, kEps);   // W siempre 1
    EXPECT_NEAR(v[1], 0.0f, kEps);   // Y
    EXPECT_NEAR(v[2], 0.0f, kEps);   // Z (horizontal)
    EXPECT_NEAR(v[3], 1.0f, kEps);   // X
    EXPECT_NEAR(v[4], 0.0f, kEps);   // V
    EXPECT_NEAR(v[5], 0.0f, kEps);
    EXPECT_NEAR(v[6], -0.5f, kEps);  // R
    EXPECT_NEAR(v[7], 0.0f, kEps);
    EXPECT_NEAR(v[8], 1.0f, kEps);   // U = cos(0) = 1
}

TEST(HoaGainMatrix, Lateral90Grados) {
    // az=90°=pi/2: cos=0, sin=1 -> X=0, Y=1, U=cos(180°)=-1, V=sin(180°)=0
    HoaVector v = HoaGainMatrix::encode(kPi / 2.0f);
    EXPECT_NEAR(v[1], 1.0f, kEps);    // Y
    EXPECT_NEAR(v[3], 0.0f, 1e-4f);   // X ~ 0
    EXPECT_NEAR(v[4], 0.0f, 1e-4f);   // V = sin(pi) ~ 0
    EXPECT_NEAR(v[8], -1.0f, kEps);   // U = cos(pi) = -1
}

TEST(HoaGainMatrix, Detras180Grados) {
    // az=180°=pi: cos=-1, sin=0 -> X=-1, Y=0, U=cos(360°)=1, V=sin(360°)=0
    HoaVector v = HoaGainMatrix::encode(kPi);
    EXPECT_NEAR(v[1], 0.0f, 1e-4f);
    EXPECT_NEAR(v[3], -1.0f, kEps);
    EXPECT_NEAR(v[4], 0.0f, 1e-4f);
    EXPECT_NEAR(v[8], 1.0f, kEps);
}

TEST(HoaGainMatrix, ElevacionSiempreCero) {
    // Los 3 canales que dependen de elevación (Z, y los dos de l=2,m=±1)
    // deben ser EXACTAMENTE 0 para cualquier azimuth, en cualquier orden —
    // es la identidad matemática documentada en el header, no un valor por
    // defecto arbitrario.
    for (float az = -3.0f; az <= 3.0f; az += 0.37f) {
        HoaVector v = HoaGainMatrix::encode(az);
        EXPECT_FLOAT_EQ(v[2], 0.0f) << "az=" << az;
        EXPECT_FLOAT_EQ(v[5], 0.0f) << "az=" << az;
        EXPECT_FLOAT_EQ(v[7], 0.0f) << "az=" << az;
        EXPECT_FLOAT_EQ(v[0], 1.0f) << "az=" << az;   // W constante
        EXPECT_FLOAT_EQ(v[6], -0.5f) << "az=" << az;  // R constante
    }
}

TEST(HoaGainMatrix, PeriodicidadDosPi) {
    // encode(az) == encode(az + 2*pi) — mismas 9 componentes, dentro de la
    // tolerancia numérica de sin/cos en float.
    for (float az = -2.5f; az <= 2.5f; az += 0.53f) {
        HoaVector a = HoaGainMatrix::encode(az);
        HoaVector b = HoaGainMatrix::encode(az + 2.0f * kPi);
        for (int ch = 0; ch < Ivanna::kHoaNumChannels; ++ch) {
            EXPECT_NEAR(a[static_cast<size_t>(ch)], b[static_cast<size_t>(ch)], 1e-3f)
                << "canal=" << ch << " az=" << az;
        }
    }
}

TEST(HoaGainMatrix, Accumulate) {
    HoaVector field{};
    HoaVector src = HoaGainMatrix::encode(0.0f);
    HoaGainMatrix::accumulate(field, src, 0.5f);
    HoaGainMatrix::accumulate(field, src, 0.5f);
    // Dos fuentes idénticas a mitad de ganancia cada una == una fuente a
    // ganancia 1.0 (linealidad de la codificación HOA, propiedad real que
    // se explota en Fase 1 del encargo — varias fuentes sumadas en el mismo
    // campo antes de decodificar).
    for (int ch = 0; ch < Ivanna::kHoaNumChannels; ++ch) {
        EXPECT_NEAR(field[static_cast<size_t>(ch)], src[static_cast<size_t>(ch)], kEps)
            << "canal=" << ch;
    }
}
