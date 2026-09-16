// test_upmixing_control_plane.cpp — evidencia del canal de control Upmixing
// app -> OmegaControlBus (SHM seqlock) -> consumidor audioserver.
//
// Cubre los puntos obligatorios del encargo:
//   - el snapshot transporta upmixing_enabled / upmixing_immersivity (POD)
//   - CRC32 valido tras publicar (integridad cross-process)
//   - lectura seqlock segura (readLatest no bloquea, generation monotonica)
//   - ON/OFF funciona y el consumidor recibe el estado
//   - ABI: tamano <= 512 bytes, trivialmente copiable, magic+version
#include <gtest/gtest.h>
#include "../include/omega_control_bus.h"
#include <cstdio>
#include <unistd.h>

using ivanna::OmegaControlBus;
using ivanna::OmegaDspSnapshot;

namespace {
const char* kTestPath = "/tmp/omega_upmixing_ctrl_test";
}

TEST(UpmixingControlPlane, SnapshotAbiEsPodYCompacto) {
    // Barrera ABI: el snapshot debe seguir siendo POD y caber en el limite.
    EXPECT_TRUE(std::is_trivially_copyable<OmegaDspSnapshot>::value);
    EXPECT_LE(sizeof(OmegaDspSnapshot), 512u);
    OmegaDspSnapshot d = OmegaDspSnapshot::makeDefault();
    EXPECT_TRUE(d.isMagicValid());
    EXPECT_TRUE(d.isVersionValid());
    EXPECT_TRUE(d.isCrcValid());
    EXPECT_EQ(d.upmixing_enabled, 0u);          // default OFF (cero degradacion)
    EXPECT_FLOAT_EQ(d.upmixing_immersivity, 1.0f);
}

TEST(UpmixingControlPlane, PublishYReadLatestTransportanUpmixing) {
    ::unlink(kTestPath);
    OmegaControlBus writer;
    ASSERT_TRUE(writer.openWriter(kTestPath));

    // Estado OFF inicial
    OmegaDspSnapshot snap = OmegaDspSnapshot::makeDefault();
    snap.upmixing_enabled = 0u;
    snap.upmixing_immersivity = 1.0f;
    ASSERT_TRUE(writer.publish(snap));

    OmegaControlBus reader;
    ASSERT_TRUE(reader.openReader(kTestPath));
    OmegaDspSnapshot got{};
    uint64_t seen = 0;
    ASSERT_TRUE(reader.readLatest(got, seen));
    EXPECT_TRUE(got.isCrcValid());                 // CRC valido en el reader
    EXPECT_EQ(got.upmixing_enabled, 0u);
    EXPECT_GT(got.generation, 0u);

    // JNI activa Upmixing -> daemon publica ON
    snap.upmixing_enabled = 1u;
    snap.upmixing_immersivity = 0.7f;
    ASSERT_TRUE(writer.publish(snap));
    ASSERT_TRUE(reader.readLatest(got, seen));     // generation nueva
    EXPECT_TRUE(got.isCrcValid());
    EXPECT_EQ(got.upmixing_enabled, 1u);           // consumidor recibe ON
    EXPECT_FLOAT_EQ(got.upmixing_immersivity, 0.7f);

    // JNI desactiva -> vuelve a OFF sin artefactos (estado consistente)
    snap.upmixing_enabled = 0u;
    ASSERT_TRUE(writer.publish(snap));
    ASSERT_TRUE(reader.readLatest(got, seen));
    EXPECT_EQ(got.upmixing_enabled, 0u);
    EXPECT_TRUE(got.isCrcValid());

    writer.close();
    reader.close();
    ::unlink(kTestPath);
}

TEST(UpmixingControlPlane, ReadLatestNoActualizaSinNuevaGeneracion) {
    ::unlink(kTestPath);
    OmegaControlBus writer;
    ASSERT_TRUE(writer.openWriter(kTestPath));
    OmegaDspSnapshot snap = OmegaDspSnapshot::makeDefault();
    snap.upmixing_enabled = 1u;
    ASSERT_TRUE(writer.publish(snap));

    OmegaControlBus reader;
    ASSERT_TRUE(reader.openReader(kTestPath));
    OmegaDspSnapshot got{};
    uint64_t seen = 0;
    ASSERT_TRUE(reader.readLatest(got, seen));
    uint64_t genTrasPrimera = got.generation;
    // Sin nuevo publish, readLatest no debe actualizar (seqlock estable).
    EXPECT_FALSE(reader.readLatest(got, seen));
    EXPECT_EQ(got.generation, genTrasPrimera);
    writer.close(); reader.close();
    ::unlink(kTestPath);
}
