// ============================================================================
// test_hrtf_database.cpp — regresión de la cadena HRTF de la rejilla:
//   HRTFDatabase (carga desde mediciones reales) -> HRTFInterpolator ->
//   BinauralRenderer / HybridRenderer (fallback observable).
//
// CAUSA RAIZ que fija: g_hrtfDatabase era `= {}` (ceros) y nada la llenaba;
// el interpolador devolvia filtros nulos y los renderizadores lo disimulaban
// con un umbral de energia. Estos tests detectan: filtros nulos, NaN/Inf,
// canales L/R intercambiados, convencion de azimut invertida, salida
// totalmente silenciosa y lecturas fuera de la tabla.
//
// Los datos sinteticos de abajo son DATOS DE PRUEBA (nunca se enlazan al
// producto). Las pruebas con IVANNA_HRTF_ASSET_DIR usan los .ihr1 reales
// (KEMAR / CIPIC medidos) que se distribuyen en el APK.
// ============================================================================
#include <gtest/gtest.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#include "spatial/HRTFDatabase.h"
#include "spatial/HRTFInterpolator.hpp"
#include "spatial/HybridRenderer.hpp"
#include "spatial/SpatialRenderer.hpp"

#ifndef IVANNA_HRTF_ASSET_DIR
#error "IVANNA_HRTF_ASSET_DIR debe definirse desde tests/CMakeLists.txt"
#endif

using namespace Ivanna;

namespace {

constexpr float kDeg2Rad = 0.017453292519943295f;

bool tableAllZero() {
    const float* p = &g_hrtfDatabase[0][0].left[0];
    const size_t n = HRTF_ELEVATIONS * HRTF_AZIMUTHS * 2 * HRTF_TAPS;
    for (size_t i = 0; i < n; ++i) if (p[i] != 0.0f) return false;
    return true;
}

float energy(const float* x, size_t n) {
    float e = 0.0f;
    for (size_t i = 0; i < n; ++i) e += x[i] * x[i];
    return e;
}

bool allFinite(const float* x, size_t n) {
    for (size_t i = 0; i < n; ++i) if (!std::isfinite(x[i])) return false;
    return true;
}

// Mediciones sinteticas con una convencion de azimut conocida. Cada HRIR es un
// impulso en el tap 2 cuyo nivel depende de que lado esta la fuente:
//   lado izquierdo -> oido izquierdo fuerte (1.0) y derecho debil (0.2).
struct Meas {
    std::vector<float> az, el, L, R;
    size_t irLen = 128;
    int sr = 48000;
    HrtfDbMeasured view() const {
        HrtfDbMeasured m;
        m.azDeg = az.data(); m.elDeg = el.data();
        m.left = L.data();   m.right = R.data();
        m.numPositions = az.size(); m.irLen = irLen; m.sampleRateHz = sr;
        return m;
    }
};

Meas makeMeas(bool leftPositive, size_t irLen = 128) {
    Meas m;
    m.irLen = irLen;
    for (int e = -60; e <= 60; e += 10) {
        for (int a = 0; a < 360; a += 10) {
            m.az.push_back(static_cast<float>(a));
            m.el.push_back(static_cast<float>(e));
            const float side = (leftPositive ? 1.0f : -1.0f) *
                               std::sin(static_cast<float>(a) * kDeg2Rad);   // +1 = izquierda
            const float gL = 0.6f + 0.4f * side;
            const float gR = 0.6f - 0.4f * side;
            std::vector<float> l(irLen, 0.0f), r(irLen, 0.0f);
            l[2] = gL; r[2] = gR;
            l[5] = 0.1f * gL; r[5] = 0.1f * gR;
            // Tap 7 depende SOLO de la elevacion: si el interpolador lee otra fila
            // (p.ej. az0=24 => fila siguiente), el valor cambia y el test lo ve.
            l[7] = r[7] = 0.05f + 0.5f * static_cast<float>(e + 60) / 120.0f;
            m.L.insert(m.L.end(), l.begin(), l.end());
            m.R.insert(m.R.end(), r.begin(), r.end());
        }
    }
    return m;
}

class HrtfDbTest : public ::testing::Test {
protected:
    void SetUp() override { hrtfDatabaseUnloadForTesting(); }
    void TearDown() override { hrtfDatabaseUnloadForTesting(); }
};

std::string asset(const char* name) {
    return (std::filesystem::path(IVANNA_HRTF_ASSET_DIR) / name).string();
}

std::vector<char> readAll(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<char>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

std::string writeTemp(const char* tag, const std::vector<char>& bytes) {
    const auto p = std::filesystem::temp_directory_path() /
                   (std::string("ivanna_hrtf_test_") + tag + ".ihr1");
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return p.string();
}

} // namespace

// ── 1. Estado vacío: explícito, nunca basura ────────────────────────────────
TEST_F(HrtfDbTest, EmptyTableIsNotReadyAndInterpolatorReportsFalseWithZeroedOutputs) {
    EXPECT_FALSE(hrtfDatabaseReady());
    EXPECT_TRUE(tableAllZero());

    float l[HRTF_TAPS], r[HRTF_TAPS];
    for (size_t i = 0; i < HRTF_TAPS; ++i) { l[i] = 7.0f; r[i] = -7.0f; }
    HRTFInterpolator interp;
    EXPECT_FALSE(interp.getInterpolatedHRTF(30.0f, 10.0f, l, r));
    for (size_t i = 0; i < HRTF_TAPS; ++i) { EXPECT_EQ(l[i], 0.0f); EXPECT_EQ(r[i], 0.0f); }
}

TEST_F(HrtfDbTest, InterpolatorRejectsNullAndNonFiniteInputsWithZeroedOutputs) {
    Meas m = makeMeas(true);
    ASSERT_EQ(hrtfDatabaseBuild(m.view(), true), HrtfDbResult::Ok);

    float l[HRTF_TAPS], r[HRTF_TAPS];
    HRTFInterpolator interp;
    EXPECT_FALSE(interp.getInterpolatedHRTF(0.0f, 0.0f, nullptr, r));
    EXPECT_FALSE(interp.getInterpolatedHRTF(0.0f, 0.0f, l, nullptr));

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (float bad : {nan, inf, -inf}) {
        for (size_t i = 0; i < HRTF_TAPS; ++i) { l[i] = 9.0f; r[i] = 9.0f; }
        EXPECT_FALSE(interp.getInterpolatedHRTF(bad, 0.0f, l, r));
        EXPECT_EQ(energy(l, HRTF_TAPS), 0.0f);
        for (size_t i = 0; i < HRTF_TAPS; ++i) { l[i] = 9.0f; r[i] = 9.0f; }
        EXPECT_FALSE(interp.getInterpolatedHRTF(0.0f, bad, l, r));
        EXPECT_EQ(energy(r, HRTF_TAPS), 0.0f);
    }
}

// ── 2. Carga válida y correspondencia L/R + convención de azimut ────────────
TEST_F(HrtfDbTest, ValidMeasuredSetLoadsAndKeepsLeftRightCorrespondence) {
    Meas m = makeMeas(/*leftPositive=*/true);
    ASSERT_EQ(hrtfDatabaseBuild(m.view(), true), HrtfDbResult::Ok);
    ASSERT_TRUE(hrtfDatabaseReady());

    float l[HRTF_TAPS], r[HRTF_TAPS];
    HRTFInterpolator interp;
    // Convención de la tabla: positivo = DERECHA. Fuente a la izquierda = 270°.
    ASSERT_TRUE(interp.getInterpolatedHRTF(270.0f, 0.0f, l, r));
    EXPECT_NEAR(l[2], 1.0f, 1e-4f) << "oido izquierdo debe ser el fuerte para fuente izquierda";
    EXPECT_NEAR(r[2], 0.2f, 1e-4f);
    EXPECT_NEAR(l[5], 0.1f, 1e-4f);   // el tap se conserva en su indice (sin desplazamiento)
    EXPECT_NEAR(l[3], 0.0f, 1e-6f);

    ASSERT_TRUE(interp.getInterpolatedHRTF(90.0f, 0.0f, l, r));
    EXPECT_NEAR(r[2], 1.0f, 1e-4f) << "fuente derecha: oido derecho fuerte";
    EXPECT_NEAR(l[2], 0.2f, 1e-4f);
}

TEST_F(HrtfDbTest, AzimuthConventionFlagIsHonoredInBothDirections) {
    // Dataset en convención "positivo = derecha": el flag false NO debe invertir.
    Meas m = makeMeas(/*leftPositive=*/false);
    ASSERT_EQ(hrtfDatabaseBuild(m.view(), /*azimuthLeftPositive=*/false), HrtfDbResult::Ok);
    float l[HRTF_TAPS], r[HRTF_TAPS];
    HRTFInterpolator interp;
    ASSERT_TRUE(interp.getInterpolatedHRTF(270.0f, 0.0f, l, r));
    EXPECT_NEAR(l[2], 1.0f, 1e-4f);
    EXPECT_NEAR(r[2], 0.2f, 1e-4f);
}

TEST_F(HrtfDbTest, BilinearMidpointEqualsAverageOfNeighbourNodes) {
    Meas m = makeMeas(true);
    // Sin el tap 7 (que depende de la elevación): así todos los nodos tienen el MISMO onset y la
    // interpolación alineada coincide con el promedio muestra a muestra. Con ITD distinta entre
    // nodos ya NO deben coincidir: ver AlignedInterpolation...
    for (size_t p = 0; p < m.az.size(); ++p) { m.L[p * m.irLen + 7] = 0.0f; m.R[p * m.irLen + 7] = 0.0f; }
    ASSERT_EQ(hrtfDatabaseBuild(m.view(), true), HrtfDbResult::Ok);
    float l[HRTF_TAPS], r[HRTF_TAPS];
    HRTFInterpolator interp;
    ASSERT_TRUE(interp.getInterpolatedHRTF(7.5f, 0.0f, l, r));   // entre az 0 y 15, el 0 (fila 3)
    for (size_t t = 0; t < HRTF_TAPS; ++t) {
        const float eL = 0.5f * (g_hrtfDatabase[3][0].left[t]  + g_hrtfDatabase[3][1].left[t]);
        const float eR = 0.5f * (g_hrtfDatabase[3][0].right[t] + g_hrtfDatabase[3][1].right[t]);
        EXPECT_NEAR(l[t], eL, 1e-6f);
        EXPECT_NEAR(r[t], eR, 1e-6f);
    }
}

// ── Interpolación alineada en el tiempo ─────────────────────────────────────────────────────
// Mediciones sintéticas: cada HRIR es UN impulso unitario cuyo retardo crece con el azimut
// (2 muestras por cada 10°: del orden de la ITD real). Promediar muestra a muestra daba DOS
// impulsos de media amplitud (peine en frecuencia: caída de agudos y ITD borrada). Alineando por
// onset debe quedar UN impulso en el retardo medio.
Meas makeDelayMeas() {
    Meas m;
    for (int e = -60; e <= 60; e += 10) {
        for (int a = 0; a < 360; a += 10) {
            m.az.push_back(static_cast<float>(a));
            m.el.push_back(static_cast<float>(e));
            std::vector<float> h(m.irLen, 0.0f);
            h[10 + static_cast<size_t>(a / 10) * 2] = 1.0f;   // retardo = 10 + 2·(az/10) <= 80 < 128
            m.L.insert(m.L.end(), h.begin(), h.end());
            m.R.insert(m.R.end(), h.begin(), h.end());
        }
    }
    return m;
}

size_t argMaxAbs(const float* x, size_t n) {
    size_t best = 0;
    for (size_t i = 1; i < n; ++i) if (std::fabs(x[i]) > std::fabs(x[best])) best = i;
    return best;
}

// Fracción de la energía total concentrada en ±1 muestra del pico: 1 = un impulso compacto;
// dos impulsos de media amplitud separados 2 muestras dan ~0.5.
double compactness(const float* x, size_t n) {
    const size_t pk = argMaxAbs(x, n);
    double win = 0.0, tot = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double e = double(x[i]) * x[i];
        tot += e;
        if (i + 1 >= pk && i <= pk + 1) win += e;
    }
    return tot > 0.0 ? win / tot : 0.0;
}

TEST_F(HrtfDbTest, AlignedInterpolationKeepsOneImpulseAtTheInterpolatedDelayNotTwoHalfImpulses) {
    Meas m = makeDelayMeas();
    ASSERT_EQ(hrtfDatabaseBuild(m.view(), /*azimuthLeftPositive=*/false), HrtfDbResult::Ok);

    // Nodo de la tabla az=15° (entre mediciones de 10° y 20°, retardos 12 y 14): ~13.
    const auto& node = g_hrtfDatabase[3][1];
    EXPECT_NEAR(static_cast<double>(argMaxAbs(node.left, HRTF_TAPS)), 13.0, 1.5);
    EXPECT_GT(compactness(node.left, HRTF_TAPS), 0.8) << "sin alinear: ~0.5 (dos impulsos)";
    EXPECT_GT(energy(node.left, HRTF_TAPS), 0.7f) << "la energía no debe caer a ~0.5";

    // En ejecución, az=20° = 2/3 del nodo 15° (retardo ~13) + 1/3 del nodo 30° (~16): ~14.
    float l[HRTF_TAPS], r[HRTF_TAPS];
    HRTFInterpolator interp;
    ASSERT_TRUE(interp.getInterpolatedHRTF(20.0f, 0.0f, l, r));
    for (const float* h : {static_cast<const float*>(l), static_cast<const float*>(r)}) {
        EXPECT_NEAR(static_cast<double>(argMaxAbs(h, HRTF_TAPS)), 14.0, 1.5);
        EXPECT_GT(compactness(h, HRTF_TAPS), 0.8);
        EXPECT_GT(energy(h, HRTF_TAPS), 0.7f);
        EXPECT_TRUE(allFinite(h, HRTF_TAPS));
    }
}

TEST_F(HrtfDbTest, AlignedInterpolationMovesSmoothlyWithTheDirectionAndKeepsTheItdMonotonic) {
    Meas m = makeDelayMeas();
    ASSERT_EQ(hrtfDatabaseBuild(m.view(), /*azimuthLeftPositive=*/false), HrtfDbResult::Ok);
    HRTFInterpolator interp;
    float l[HRTF_TAPS], r[HRTF_TAPS];
    double prev = -1.0;
    for (float az = 30.0f; az <= 60.0f; az += 1.0f) {
        ASSERT_TRUE(interp.getInterpolatedHRTF(az, 0.0f, l, r));
        // Centroide de energía = retardo efectivo: debe crecer sin saltos con el azimut.
        double num = 0.0, den = 0.0;
        for (size_t t = 0; t < HRTF_TAPS; ++t) { num += t * double(l[t]) * l[t]; den += double(l[t]) * l[t]; }
        ASSERT_GT(den, 0.5);
        const double c = num / den;
        if (prev >= 0.0) {
            EXPECT_GE(c, prev - 1e-3) << "az=" << az;
            EXPECT_LT(c - prev, 1.0) << "salto de retardo > 1 muestra entre grados contiguos, az=" << az;
        }
        prev = c;
    }
}

TEST_F(HrtfDbTest, EveryGridNodeIsFiniteAndNonSilentInBothEars) {
    Meas m = makeMeas(true);
    ASSERT_EQ(hrtfDatabaseBuild(m.view(), true), HrtfDbResult::Ok);
    for (size_t e = 0; e < HRTF_ELEVATIONS; ++e) {
        for (size_t a = 0; a < HRTF_AZIMUTHS; ++a) {
            ASSERT_TRUE(allFinite(g_hrtfDatabase[e][a].left,  HRTF_TAPS));
            ASSERT_TRUE(allFinite(g_hrtfDatabase[e][a].right, HRTF_TAPS));
            EXPECT_GT(energy(g_hrtfDatabase[e][a].left,  HRTF_TAPS), 0.0f) << e << "," << a;
            EXPECT_GT(energy(g_hrtfDatabase[e][a].right, HRTF_TAPS), 0.0f) << e << "," << a;
        }
    }
}

// ── 3. Bug de índice: az -> 360.0f leía fuera de la fila (y de la tabla) ────
TEST_F(HrtfDbTest, AzimuthWrapAroundNeverReadsOutOfTable) {
    Meas m = makeMeas(true);
    ASSERT_EQ(hrtfDatabaseBuild(m.view(), true), HrtfDbResult::Ok);
    HRTFInterpolator interp;
    float ref[HRTF_TAPS], refR[HRTF_TAPS], l[HRTF_TAPS], r[HRTF_TAPS];
    ASSERT_TRUE(interp.getInterpolatedHRTF(0.0f, 0.0f, ref, refR));

    // -1e-9f + 360.0f == 360.0f exacto en float: antes az0 valía 24 (fuera de fila).
    for (float az : {-1e-9f, -0.0f, 0.0f, 360.0f, -360.0f, 720.0f, 359.99997f, 1e30f, -1e30f}) {
        ASSERT_TRUE(interp.getInterpolatedHRTF(az, 0.0f, l, r)) << az;
        EXPECT_TRUE(allFinite(l, HRTF_TAPS) && allFinite(r, HRTF_TAPS)) << az;
        EXPECT_GT(energy(l, HRTF_TAPS), 0.0f) << az;
    }
    ASSERT_TRUE(interp.getInterpolatedHRTF(-1e-9f, 0.0f, l, r));
    for (size_t t = 0; t < HRTF_TAPS; ++t) {
        EXPECT_FLOAT_EQ(l[t], ref[t]) << "az=-1e-9 debe equivaler a az=0 (misma elevacion)";
        EXPECT_FLOAT_EQ(r[t], refR[t]);
    }
    // Última fila de elevación + wrap: el caso que salía de la tabla completa.
    ASSERT_TRUE(interp.getInterpolatedHRTF(-1e-9f, 45.0f, l, r));
    ASSERT_TRUE(interp.getInterpolatedHRTF(0.0f, 1000.0f, l, r));   // elevacion se acota a +45
    ASSERT_TRUE(interp.getInterpolatedHRTF(0.0f, -1000.0f, l, r));  // y a -45
}

// ── 4. Truncado de HRIR largos ──────────────────────────────────────────────
TEST_F(HrtfDbTest, LongHrirIsTruncatedTo128TapsWithShortFadeOut) {
    Meas m = makeMeas(true, /*irLen=*/512);
    for (size_t p = 0; p < m.az.size(); ++p) {
        m.L[p * 512 + 100] = 0.5f;    // dentro de la ventana util: se conserva
        m.L[p * 512 + 127] = 1.0f;    // en el borde: atenuado por la ventana
        m.L[p * 512 + 300] = 9.0f;    // fuera de 128 taps: descartado
    }
    ASSERT_EQ(hrtfDatabaseBuild(m.view(), true), HrtfDbResult::Ok);
    float l[HRTF_TAPS], r[HRTF_TAPS];
    HRTFInterpolator interp;
    ASSERT_TRUE(interp.getInterpolatedHRTF(0.0f, 0.0f, l, r));
    EXPECT_NEAR(l[100], 0.5f, 1e-5f);
    EXPECT_LT(std::fabs(l[127]), 0.02f);
    EXPECT_TRUE(allFinite(l, HRTF_TAPS));
}

// ── 5. Rechazos explícitos: la tabla queda vacía, nunca a medias ────────────
TEST_F(HrtfDbTest, RejectsBadInputsAndLeavesTableEmpty) {
    {   // punteros / tamaños inválidos
        HrtfDbMeasured m;
        EXPECT_EQ(hrtfDatabaseBuild(m, true), HrtfDbResult::InvalidArgs);
    }
    {   // frecuencia distinta a la del motor
        Meas m = makeMeas(true); m.sr = 44100;
        EXPECT_EQ(hrtfDatabaseBuild(m.view(), true), HrtfDbResult::SampleRateMismatch);
    }
    {   // NaN en un tap usado
        Meas m = makeMeas(true); m.L[3 * m.irLen + 3] = std::numeric_limits<float>::quiet_NaN();
        EXPECT_EQ(hrtfDatabaseBuild(m.view(), true), HrtfDbResult::NonFiniteData);
    }
    {   // Inf en un tap usado del canal derecho
        Meas m = makeMeas(true); m.R[10 * m.irLen + 1] = std::numeric_limits<float>::infinity();
        EXPECT_EQ(hrtfDatabaseBuild(m.view(), true), HrtfDbResult::NonFiniteData);
    }
    {   // azimut no finito
        Meas m = makeMeas(true); m.az[5] = std::numeric_limits<float>::quiet_NaN();
        EXPECT_EQ(hrtfDatabaseBuild(m.view(), true), HrtfDbResult::NonFiniteData);
    }
    {   // cobertura insuficiente: solo un cuarto de la esfera
        Meas m;
        for (int a = 0; a <= 90; a += 10) {
            m.az.push_back(static_cast<float>(a)); m.el.push_back(0.0f);
            std::vector<float> h(128, 0.0f); h[2] = 1.0f;
            m.L.insert(m.L.end(), h.begin(), h.end()); m.R.insert(m.R.end(), h.begin(), h.end());
        }
        EXPECT_EQ(hrtfDatabaseBuild(m.view(), true), HrtfDbResult::InsufficientCoverage);
    }
    {   // cobertura completa pero HRIR todo-ceros: filtro silencioso
        Meas m = makeMeas(true);
        std::fill(m.L.begin(), m.L.end(), 0.0f);
        EXPECT_EQ(hrtfDatabaseBuild(m.view(), true), HrtfDbResult::SilentFilter);
    }
    EXPECT_FALSE(hrtfDatabaseReady());
    EXPECT_TRUE(tableAllZero()) << "un fallo no debe dejar la tabla a medio rellenar";
}

TEST_F(HrtfDbTest, SecondLoadIsRejectedAndKeepsPublishedTable) {
    Meas a = makeMeas(true);
    ASSERT_EQ(hrtfDatabaseBuild(a.view(), true), HrtfDbResult::Ok);
    const float before = g_hrtfDatabase[3][5].left[2];
    Meas b = makeMeas(true);
    std::fill(b.L.begin(), b.L.end(), 0.3f);
    EXPECT_EQ(hrtfDatabaseBuild(b.view(), true), HrtfDbResult::AlreadyLoaded);
    EXPECT_EQ(g_hrtfDatabase[3][5].left[2], before);
    EXPECT_TRUE(hrtfDatabaseReady());
}

// ── 6. Archivos reales .ihr1 (KEMAR / CIPIC) ───────────────────────────────
TEST_F(HrtfDbTest, RealKemarDatasetLoadsAndLeftSourceIsLouderInLeftEar) {
    ASSERT_EQ(hrtfDatabaseLoadFile(asset("kemar.ihr1").c_str()), HrtfDbResult::Ok);
    ASSERT_TRUE(hrtfDatabaseReady());
    HRTFInterpolator interp;
    float l[HRTF_TAPS], r[HRTF_TAPS];

    // Evidencia medida en el asset: az=+90 (convención SOFA) => oído izquierdo
    // más fuerte (RMS 0.0623 vs 0.0161). La tabla usa positivo = derecha, así
    // que la fuente a la izquierda es 270°. Si la conversión se rompiera, los
    // canales saldrían intercambiados y estos EXPECT fallarían.
    ASSERT_TRUE(interp.getInterpolatedHRTF(270.0f, 0.0f, l, r));
    EXPECT_GT(energy(l, HRTF_TAPS), 2.0f * energy(r, HRTF_TAPS));
    ASSERT_TRUE(interp.getInterpolatedHRTF(90.0f, 0.0f, l, r));
    EXPECT_GT(energy(r, HRTF_TAPS), 2.0f * energy(l, HRTF_TAPS));
    ASSERT_TRUE(interp.getInterpolatedHRTF(0.0f, 0.0f, l, r));
    EXPECT_NEAR(energy(l, HRTF_TAPS) / energy(r, HRTF_TAPS), 1.0f, 0.15f)
        << "frente: ambos oidos comparables";

    for (size_t e = 0; e < HRTF_ELEVATIONS; ++e) {
        for (size_t a = 0; a < HRTF_AZIMUTHS; ++a) {
            ASSERT_TRUE(allFinite(g_hrtfDatabase[e][a].left,  HRTF_TAPS));
            ASSERT_TRUE(allFinite(g_hrtfDatabase[e][a].right, HRTF_TAPS));
            ASSERT_GT(energy(g_hrtfDatabase[e][a].left,  HRTF_TAPS), 1e-9f);
            ASSERT_GT(energy(g_hrtfDatabase[e][a].right, HRTF_TAPS), 1e-9f);
        }
    }
}

TEST_F(HrtfDbTest, OtherShippedDatasetsLoadWithSameAzimuthConvention) {
    // KEMAR large, CIPIC (hueco de 26.8 deg en la esquina lateral -45 deg: aceptado
    // por el umbral de 30 deg) y freefield: misma convencion => mismo lado/oido.
    for (const char* name : {"kemar_large.ihr1", "cipic_003.ihr1", "cipic_165.ihr1",
                             "freefield_demo.ihr1"}) {
        hrtfDatabaseUnloadForTesting();
        ASSERT_EQ(hrtfDatabaseLoadFile(asset(name).c_str()), HrtfDbResult::Ok) << name;
        HRTFInterpolator interp;
        float l[HRTF_TAPS], r[HRTF_TAPS];
        ASSERT_TRUE(interp.getInterpolatedHRTF(270.0f, 0.0f, l, r));
        EXPECT_GT(energy(l, HRTF_TAPS), energy(r, HRTF_TAPS)) << name << ": fuente izquierda";
        ASSERT_TRUE(interp.getInterpolatedHRTF(90.0f, 0.0f, l, r));
        EXPECT_GT(energy(r, HRTF_TAPS), energy(l, HRTF_TAPS)) << name << ": fuente derecha";
    }
}

TEST_F(HrtfDbTest, HorizontalPlaneOnlyDatasetIsRejectedInsteadOfInventingElevation) {
    // tu_berlin_kemar solo tiene elevacion 0 (peor nodo a 45 deg): sin cobertura
    // de la rejilla +-45 deg. Debe fallar de forma explicita, no rellenar.
    EXPECT_EQ(hrtfDatabaseLoadFile(asset("tu_berlin_kemar.ihr1").c_str()),
              HrtfDbResult::InsufficientCoverage);
    EXPECT_FALSE(hrtfDatabaseReady());
    EXPECT_TRUE(tableAllZero());
}

TEST_F(HrtfDbTest, TruncatedCorruptAndOutOfRangeFilesAreRejected) {
    const std::vector<char> good = readAll(asset("kemar.ihr1"));
    ASSERT_GT(good.size(), 1000u);

    auto expectRejected = [&](const char* tag, std::vector<char> bytes, HrtfDbResult want) {
        const std::string p = writeTemp(tag, bytes);
        EXPECT_EQ(hrtfDatabaseLoadFile(p.c_str()), want) << tag;
        EXPECT_FALSE(hrtfDatabaseReady()) << tag;
        EXPECT_TRUE(tableAllZero()) << tag;
        std::filesystem::remove(p);
    };

    expectRejected("truncated", std::vector<char>(good.begin(), good.end() - 100),
                   HrtfDbResult::FileUnreadable);
    expectRejected("headeronly", std::vector<char>(good.begin(), good.begin() + 16),
                   HrtfDbResult::FileUnreadable);
    expectRejected("empty", {}, HrtfDbResult::FileUnreadable);

    {   // numPositions = 0
        std::vector<char> b = good; const uint32_t v = 0; std::memcpy(&b[4], &v, 4);
        expectRejected("numpos0", b, HrtfDbResult::FileUnreadable);
    }
    {   // numPositions descomunal (cabecera corrupta: no debe reservar memoria)
        std::vector<char> b = good; const uint32_t v = 0xFFFFFFFFu; std::memcpy(&b[4], &v, 4);
        expectRejected("numposhuge", b, HrtfDbResult::FileUnreadable);
    }
    {   // irLen fuera de rango
        std::vector<char> b = good; const uint32_t v = 9000; std::memcpy(&b[8], &v, 4);
        expectRejected("irlen9000", b, HrtfDbResult::FileUnreadable);
    }
    {   // magic incorrecto
        std::vector<char> b = good; b[0] = 'X';
        expectRejected("badmagic", b, HrtfDbResult::FileUnreadable);
    }
    {   // frecuencia declarada distinta a la del motor (el lector la acepta; la tabla no)
        std::vector<char> b = good; const uint32_t v = 44100; std::memcpy(&b[12], &v, 4);
        expectRejected("sr44100", b, HrtfDbResult::SampleRateMismatch);
    }
    {   // NaN plantado en un HRIR (primer tap del primer par, tras la tabla az/el)
        std::vector<char> b = good;
        uint32_t numPos = 0; std::memcpy(&numPos, &good[4], 4);
        const float nan = std::numeric_limits<float>::quiet_NaN();
        // Un NaN en CADA posicion garantiza que uno cae en un tap usado.
        uint32_t irLen = 0; std::memcpy(&irLen, &good[8], 4);
        for (uint32_t p = 0; p < numPos; ++p) {
            const size_t off = 16 + static_cast<size_t>(numPos) * 8 +
                               static_cast<size_t>(p) * 2 * irLen * 4;
            std::memcpy(&b[off], &nan, 4);
        }
        expectRejected("nan", b, HrtfDbResult::NonFiniteData);
    }
    EXPECT_EQ(hrtfDatabaseLoadFile("/definitely/not/here.ihr1"), HrtfDbResult::FileUnreadable);
    EXPECT_EQ(hrtfDatabaseLoadFile(nullptr), HrtfDbResult::InvalidArgs);
}

TEST_F(HrtfDbTest, CandidateChainSkipsBadPathsAndNeverPretendsSuccess) {
    const std::string missing = "/definitely/not/here.ihr1";
    const std::string kemar = asset("kemar.ihr1");
    const char* bad[] = {missing.c_str(), nullptr};
    EXPECT_EQ(hrtfDatabaseLoadFromCandidates(bad, 2), nullptr);
    EXPECT_FALSE(hrtfDatabaseReady());

    const char* mixed[] = {missing.c_str(), kemar.c_str()};
    EXPECT_STREQ(hrtfDatabaseLoadFromCandidates(mixed, 2), kemar.c_str());
    EXPECT_TRUE(hrtfDatabaseReady());

    // Ya cargada: no recarga y no finge haber cargado otra.
    EXPECT_EQ(hrtfDatabaseLoadFromCandidates(mixed, 2), nullptr);
    EXPECT_TRUE(hrtfDatabaseReady());
}

TEST_F(HrtfDbTest, DefaultChainOnHostWithoutDeviceAssetsFailsExplicitly) {
    EXPECT_EQ(hrtfDatabaseLoadDefault(), nullptr);
    EXPECT_FALSE(hrtfDatabaseReady());
}

// ── 7. Publicación segura frente a un lector concurrente ───────────────────
TEST_F(HrtfDbTest, ConcurrentReaderNeverObservesPartiallyBuiltTable) {
    Meas m = makeMeas(true);
    std::atomic<bool> stop{false};
    std::atomic<long> trueCount{0};
    std::atomic<bool> torn{false};

    std::thread reader([&] {
        HRTFInterpolator interp;
        float l[HRTF_TAPS], r[HRTF_TAPS];
        while (!stop.load(std::memory_order_relaxed)) {
            if (interp.getInterpolatedHRTF(270.0f, 0.0f, l, r)) {
                trueCount.fetch_add(1, std::memory_order_relaxed);
                // Cada nodo cargado tiene el tap 2 >= 0.2; uno a medio construir daria 0.
                if (!(l[2] > 0.19f) || !(r[2] > 0.0f)) torn.store(true);
            } else if (energy(l, HRTF_TAPS) != 0.0f) {
                torn.store(true);   // false debe dejar salidas a cero
            }
        }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    ASSERT_EQ(hrtfDatabaseBuild(m.view(), true), HrtfDbResult::Ok);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    stop.store(true);
    reader.join();
    EXPECT_FALSE(torn.load());
    EXPECT_GT(trueCount.load(), 0);
}

// ── 8. Renderizadores: fallback observable y recarga tardía ─────────────────
TEST_F(HrtfDbTest, BinauralRendererFlagsFallbackThenSwitchesToMeasuredWhenTableLoads) {
    BinauralRenderer r;
    std::vector<float> in(256, 0.0f); in[0] = 1.0f;
    std::vector<float> oL(256), oR(256);
    const Vector3D left{-1.0f, 0.0f, 0.0f};

    EXPECT_TRUE(r.usingFallbackHrtf());
    r.processBinaural(in.data(), oL.data(), oR.data(), 256, left);
    EXPECT_TRUE(r.usingFallbackHrtf()) << "sin tabla: debe quedar marcado como respaldo";
    EXPECT_GT(energy(oL.data(), 256), 0.0f) << "el respaldo NUNCA debe ser silencio";
    EXPECT_TRUE(allFinite(oL.data(), 256) && allFinite(oR.data(), 256));

    ASSERT_EQ(hrtfDatabaseLoadFile(asset("kemar.ihr1").c_str()), HrtfDbResult::Ok);

    // Misma posicion (el angulo no cambia): la carga tardia debe forzar el recalculo.
    BinauralRenderer r2 = r;   // conserva _lastAz y filtros de respaldo
    std::vector<float> in2(256, 0.0f); in2[0] = 1.0f;
    r2.processBinaural(in2.data(), oL.data(), oR.data(), 256, left);
    EXPECT_FALSE(r2.usingFallbackHrtf()) << "tras cargar la tabla debe usar HRIR medidos";
    EXPECT_TRUE(allFinite(oL.data(), 256) && allFinite(oR.data(), 256));

    // La respuesta al impulso ES la HRIR interpolada (distGain = 1 a 1 m).
    HRTFInterpolator interp;
    float hl[HRTF_TAPS], hr[HRTF_TAPS];
    // Mismo azimut que calcula el renderizador: atan2(rx, max(1e-4, ry)) en grados.
    const float az = std::atan2(-1.0f, 1.0e-4f) * 57.2957795f;
    ASSERT_TRUE(interp.getInterpolatedHRTF(az, 0.0f, hl, hr));
    for (size_t k = 0; k < HRTF_TAPS; ++k) {
        EXPECT_NEAR(oL[k], hl[k], 1e-5f) << k;
        EXPECT_NEAR(oR[k], hr[k], 1e-5f) << k;
    }
    EXPECT_GT(energy(oL.data(), 256), 2.0f * energy(oR.data(), 256)) << "fuente izquierda";
}

TEST_F(HrtfDbTest, HybridRendererFlagsFallbackThenUsesMeasuredHrtfWithCorrectEars) {
    auto run = [](bool impulseOnLeft, float* eL, float* eR, bool* fallback) {
        HybridRenderer h;
        h.setBinauralWet(1.0f);
        h.setRoomParameters(0.3f, 0.5f, 0.5f, 0.0f);
        std::vector<float> in(2 * 256, 0.0f), out(2 * 256, 0.0f);
        in[impulseOnLeft ? 0 : 1] = 1.0f;
        h.renderBinaural(in.data(), out.data(), 256);
        *fallback = h.usingFallbackHrtf();
        float l = 0, r = 0;
        for (size_t i = 0; i < 256; ++i) { l += out[2 * i] * out[2 * i]; r += out[2 * i + 1] * out[2 * i + 1]; }
        *eL = l; *eR = r;
        EXPECT_TRUE(allFinite(out.data(), out.size()));
    };

    float eL = 0, eR = 0; bool fb = false;
    run(true, &eL, &eR, &fb);
    EXPECT_TRUE(fb) << "sin tabla el renderizador hibrido debe declarar respaldo";
    EXPECT_GT(eL + eR, 0.0f) << "el respaldo no debe ser silencio total";

    ASSERT_EQ(hrtfDatabaseLoadFile(asset("kemar.ihr1").c_str()), HrtfDbResult::Ok);

    run(true, &eL, &eR, &fb);
    EXPECT_FALSE(fb);
    EXPECT_GT(eL, eR) << "impulso en canal izquierdo -> oido izquierdo mas fuerte (L/R no intercambiados)";
    run(false, &eL, &eR, &fb);
    EXPECT_FALSE(fb);
    EXPECT_GT(eR, eL) << "impulso en canal derecho -> oido derecho mas fuerte";
}

TEST_F(HrtfDbTest, HybridRendererAlreadyRunningPicksUpTableLoadedLater) {
    HybridRenderer h;
    h.setBinauralWet(1.0f);
    h.setRoomParameters(0.3f, 0.5f, 0.5f, 0.0f);
    std::vector<float> in(2 * 128, 0.0f), out(2 * 128, 0.0f);
    h.renderBinaural(in.data(), out.data(), 128);
    ASSERT_TRUE(h.usingFallbackHrtf());

    ASSERT_EQ(hrtfDatabaseLoadFile(asset("kemar.ihr1").c_str()), HrtfDbResult::Ok);
    // Sin cambiar ningun parametro: el siguiente bloque debe recalcular.
    h.renderBinaural(in.data(), out.data(), 128);
    EXPECT_FALSE(h.usingFallbackHrtf());
}
