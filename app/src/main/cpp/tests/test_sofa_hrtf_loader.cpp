/**
 * test_sofa_hrtf_loader.cpp — SofaHRTFLoader: qué hace de verdad y qué NO hace
 *
 * load() solo valida la FIRMA HDF5 (8 bytes) y RECONSTRUYE un HRIR desde la base maestra
 * SOFA-PCA; no lee las mediciones del archivo. Estas pruebas fijan ese comportamiento para que
 * ningún consumidor vuelva a presentarlo como "medido":
 *   1. archivo inexistente / demasiado pequeño / sin firma HDF5 -> error explícito y sin HRIR.
 *   2. firma válida -> true con Status::VALID_NOT_PARSED e isReconstructedFromMasterBasis().
 *   3. el HRIR es finito, no nulo y distinto por oído (no mono ni silencio).
 *   4. el contenido del archivo NO influye; la RUTA sí (elige el sujeto entrenado).
 */
#include <gtest/gtest.h>

#include <unistd.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "SofaHRTFLoader.hpp"
#include "spatial/SofaSafRirMasterKnowledge.hpp"

namespace fs = std::filesystem;
using Ivanna::SofaHRTFLoader;

namespace {
class SofaLoaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = fs::temp_directory_path() / ("ivanna_sofa_test_" + std::to_string(::getpid()));
        fs::remove_all(dir_);
        fs::create_directories(dir_);
    }
    void TearDown() override { fs::remove_all(dir_); }

    // Escribe un archivo; si hdf5 es true empieza con la firma HDF5 completa.
    std::string write(const std::string& sub, const std::string& name, size_t size,
                      bool hdf5, uint8_t fill) {
        const fs::path d = dir_ / sub;
        fs::create_directories(d);
        const fs::path p = d / name;
        std::vector<uint8_t> buf(size, fill);
        if (hdf5 && size >= 8) {
            const uint8_t sig[8] = {0x89, 'H', 'D', 'F', 0x0D, 0x0A, 0x1A, 0x0A};
            for (int i = 0; i < 8; ++i) buf[i] = sig[i];
        }
        std::ofstream f(p, std::ios::binary);
        f.write(reinterpret_cast<const char*>(buf.data()), (std::streamsize)buf.size());
        return p.string();
    }
    fs::path dir_;
};
} // namespace

TEST_F(SofaLoaderTest, MissingFileFailsExplicitlyWithoutHrir) {
    SofaHRTFLoader l;
    EXPECT_FALSE(l.load((dir_ / "no_existe.sofa").string()));
    EXPECT_EQ(l.lastStatus(), SofaHRTFLoader::Status::FILE_NOT_FOUND);
    EXPECT_FALSE(l.isReconstructedFromMasterBasis());
    EXPECT_TRUE(l.hrtf().left.empty());
    EXPECT_TRUE(l.hrtf().right.empty());
}

TEST_F(SofaLoaderTest, TruncatedFileIsCorruptNotSilentlyAccepted) {
    SofaHRTFLoader l;
    EXPECT_FALSE(l.load(write("a", "trunc.sofa", 100, /*hdf5=*/true, 0x11)));
    EXPECT_EQ(l.lastStatus(), SofaHRTFLoader::Status::CORRUPT);
    EXPECT_TRUE(l.hrtf().left.empty());
}

TEST_F(SofaLoaderTest, FileWithoutHdf5SignatureIsRejected) {
    SofaHRTFLoader l;
    EXPECT_FALSE(l.load(write("a", "fake.sofa", 4096, /*hdf5=*/false, 'A')));
    EXPECT_EQ(l.lastStatus(), SofaHRTFLoader::Status::NOT_HDF5);
    EXPECT_FALSE(l.isReconstructedFromMasterBasis());
    EXPECT_TRUE(l.hrtf().left.empty());
}

TEST_F(SofaLoaderTest, SignatureOnlyAcceptsAsReconstructedNeverAsMeasured) {
    SofaHRTFLoader l;
    ASSERT_TRUE(l.load(write("a", "subject.sofa", 2048, /*hdf5=*/true, 0x00)));
    EXPECT_EQ(l.lastStatus(), SofaHRTFLoader::Status::VALID_NOT_PARSED);
    EXPECT_TRUE(l.isReconstructedFromMasterBasis());   // contrato: NO es una medición del archivo
}

TEST_F(SofaLoaderTest, ReconstructedHrirIsFiniteNonZeroAndDifferentPerEar) {
    SofaHRTFLoader l;
    ASSERT_TRUE(l.load(write("a", "subject.sofa", 2048, true, 0x00)));
    const auto& h = l.hrtf();
    ASSERT_EQ((int)h.left.size(), ivanna::master::kMasterHrirLen);
    ASSERT_EQ((int)h.right.size(), ivanna::master::kMasterHrirLen);
    EXPECT_FLOAT_EQ(h.sampleRate, 48000.0f);
    double eL = 0.0, eR = 0.0; bool differ = false;
    for (int i = 0; i < ivanna::master::kMasterHrirLen; ++i) {
        ASSERT_TRUE(std::isfinite(h.left[i]) && std::isfinite(h.right[i])) << "tap " << i;
        eL += (double)h.left[i] * h.left[i];
        eR += (double)h.right[i] * h.right[i];
        if (h.left[i] != h.right[i]) differ = true;
    }
    EXPECT_GT(eL, 1.0e-12);        // no es el filtro nulo
    EXPECT_GT(eR, 1.0e-12);
    EXPECT_TRUE(differ);           // oído izquierdo != derecho (no mono)
}

TEST_F(SofaLoaderTest, ContentDoesNotMatterButPathSelectsTheSubject) {
    SofaHRTFLoader a, b, c, d;
    // Mismo nombre sin id de sujeto, contenido totalmente distinto -> HRIR idéntico.
    ASSERT_TRUE(a.load(write("x", "medida.sofa", 2048, true, 0x00)));
    ASSERT_TRUE(b.load(write("y", "medida.sofa", 5000, true, 0xFF)));
    EXPECT_EQ(a.hrtf().left, b.hrtf().left);
    EXPECT_EQ(a.hrtf().right, b.hrtf().right);
    // La RUTA con el id de un sujeto entrenado cambia el resultado.
    ASSERT_TRUE(c.load(write("z", "cipic_003.sofa", 2048, true, 0x00)));
    ASSERT_TRUE(d.load(write("w", "kemar.sofa", 2048, true, 0x00)));
    EXPECT_NE(c.hrtf().left, d.hrtf().left);
}

// ── Selección de sujeto: coincidencia MÁS LARGA, no la primera ──────────────────────────────
// La tabla de anclas lista "kemar" antes que "kemar_large" y "tu_berlin_kemar", y la ruta se
// busca por subcadena: con "primera coincidencia" esos dos sujetos recibían el ancla de KEMAR
// estándar y la elección del usuario no tenía efecto.
TEST_F(SofaLoaderTest, LongestSubjectIdWinsOverShorterSubstring) {
    SofaHRTFLoader kemar, large, berlin;
    ASSERT_TRUE(kemar.load(write("a", "kemar.sofa", 2048, true, 0x00)));
    ASSERT_TRUE(large.load(write("b", "kemar_large.sofa", 2048, true, 0x00)));
    ASSERT_TRUE(berlin.load(write("c", "tu_berlin_kemar.sofa", 2048, true, 0x00)));
    EXPECT_NE(kemar.hrtf().left, large.hrtf().left)
        << "kemar_large no debe resolverse al ancla de kemar";
    EXPECT_NE(kemar.hrtf().left, berlin.hrtf().left)
        << "tu_berlin_kemar no debe resolverse al ancla de kemar";
    EXPECT_NE(large.hrtf().left, berlin.hrtf().left);
}

// ── loadCached: un acierto de caché no puede devolver éxito con el HRIR vacío ───────────────
// La caché (ruta + ok) es GLOBAL, pero hrtf() es de cada instancia. Antes, una segunda instancia
// que pedía la misma ruta recibía true sin haber cargado nada y leía un HRIR vacío.
TEST_F(SofaLoaderTest, LoadCachedHitOnAnotherInstanceStillPopulatesItsOwnHrir) {
    const std::string p = write("d", "cipic_008.sofa", 2048, true, 0x00);
    SofaHRTFLoader first, second;
    ASSERT_TRUE(first.loadCached(p));
    ASSERT_FALSE(first.hrtf().left.empty());
    ASSERT_TRUE(second.loadCached(p));
    EXPECT_FALSE(second.hrtf().left.empty())
        << "loadCached devolvió true pero esta instancia no tiene HRIR";
    EXPECT_EQ(second.hrtf().left.size(), second.hrtf().right.size());
    EXPECT_EQ(first.hrtf().left, second.hrtf().left);
    EXPECT_TRUE(second.isReconstructedFromMasterBasis());
}

TEST_F(SofaLoaderTest, LoadCachedSameInstanceSecondCallIsStillValid) {
    const std::string p = write("e", "kemar.sofa", 2048, true, 0x00);
    SofaHRTFLoader l;
    ASSERT_TRUE(l.loadCached(p));
    const auto left = l.hrtf().left;
    ASSERT_TRUE(l.loadCached(p));
    EXPECT_EQ(l.hrtf().left, left);
}

// Concurrencia: las estáticas de la caché se compartían sin exclusión mutua (carrera de datos
// sobre un std::string). Con ASan/TSan o sin ellos, esto no debe corromper ni caerse.
TEST_F(SofaLoaderTest, LoadCachedFromManyThreadsIsSafe) {
    const std::string p1 = write("f", "kemar.sofa", 2048, true, 0x00);
    const std::string p2 = write("g", "cipic_165.sofa", 2048, true, 0x00);
    std::vector<std::thread> ts;
    std::atomic<int> bad{0};
    for (int t = 0; t < 4; ++t) {
        ts.emplace_back([&, t] {
            SofaHRTFLoader l;
            for (int i = 0; i < 200; ++i) {
                if (!l.loadCached((i + t) % 2 ? p1 : p2) || l.hrtf().left.empty()) ++bad;
            }
        });
    }
    for (auto& th : ts) th.join();
    EXPECT_EQ(bad.load(), 0);
}
