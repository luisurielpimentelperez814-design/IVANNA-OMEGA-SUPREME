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
const char* kTestPath1 = "./omega_upmixing_ctrl_test_1";
const char* kTestPath2 = "./omega_upmixing_ctrl_test_2";
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
    ::unlink(kTestPath1);
    OmegaControlBus writer;
    ASSERT_TRUE(writer.openWriter(kTestPath1));

    // Estado OFF inicial
    OmegaDspSnapshot snap = OmegaDspSnapshot::makeDefault();
    snap.upmixing_enabled = 0u;
    snap.upmixing_immersivity = 1.0f;
    ASSERT_TRUE(writer.publish(snap));

    OmegaControlBus reader;
    ASSERT_TRUE(reader.openReader(kTestPath1));
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
    ::unlink(kTestPath1);
}

TEST(UpmixingControlPlane, ReadLatestNoActualizaSinNuevaGeneracion) {
    ::unlink(kTestPath2);
    OmegaControlBus writer;
    ASSERT_TRUE(writer.openWriter(kTestPath2));
    OmegaDspSnapshot snap = OmegaDspSnapshot::makeDefault();
    snap.upmixing_enabled = 1u;
    ASSERT_TRUE(writer.publish(snap));

    OmegaControlBus reader;
    ASSERT_TRUE(reader.openReader(kTestPath2));
    OmegaDspSnapshot got{};
    uint64_t seen = 0;
    ASSERT_TRUE(reader.readLatest(got, seen));
    uint64_t genTrasPrimera = got.generation;
    // Sin nuevo publish, readLatest no debe actualizar (seqlock estable).
    EXPECT_FALSE(reader.readLatest(got, seen));
    EXPECT_EQ(got.generation, genTrasPrimera);
    writer.close(); reader.close();
    ::unlink(kTestPath2);
}

TEST(UpmixingControlPlane, SupremeFiveAxes_RoundTripThroughSeqlock) {
    constexpr const char* kTestPath3 = "/tmp/ivanna_test_omega_ctrl_v2_supreme5";
    ::unlink(kTestPath3);
    OmegaControlBus writer;
    OmegaControlBus reader;
    ASSERT_TRUE(writer.openWriter(kTestPath3));
    ASSERT_TRUE(reader.openReader(kTestPath3));

    OmegaDspSnapshot pub = OmegaDspSnapshot::makeDefault();
    pub.flags |= ivanna::OMEGA_FLAG_SUPREME_LATTICE_ON
              |  ivanna::OMEGA_FLAG_SUPREME_MICROCHIRP_ON
              |  ivanna::OMEGA_FLAG_SUPREME_CVNN_ON
              |  ivanna::OMEGA_FLAG_SUPREME_SNN_HOA_ON
              |  ivanna::OMEGA_FLAG_SUPREME_PINNA_ON
              |  ivanna::OMEGA_FLAG_SUPREME_FARROW_MSO_ON
              |  ivanna::OMEGA_FLAG_SUPREME_EBPF_BYPASS_ON;
    pub.supreme_lattice_bl_drive    = 1.45f;
    pub.supreme_lattice_lambda      = 0.756f;
    pub.supreme_cvnn_harmonic_gain  = 0.62f;
    pub.supreme_cvnn_imd_cancel     = 0.88f;
    pub.supreme_snn_immersivity     = 0.79f;
    pub.supreme_snn_spike_threshold = 0.39f;
    pub.supreme_pinna_wet_mix       = 0.71f;
    pub.supreme_pinna_concha_depth  = 0.21f;
    pub.supreme_pinna_helix_curl    = -0.09f;
    pub.supreme_pinna_head_width    = 0.14f;
    pub.supreme_mso_itd_ns          = 6400.0f;

    ASSERT_TRUE(writer.publish(pub));

    OmegaDspSnapshot got{};
    uint64_t seenGen = 0;
    ASSERT_TRUE(reader.readLatest(got, seenGen));
    EXPECT_TRUE(got.isValid());
    EXPECT_EQ(got.version, ivanna::OMEGA_CTRL_VERSION);
    EXPECT_NE(got.flags & ivanna::OMEGA_FLAG_SUPREME_LATTICE_ON, 0u);
    EXPECT_NE(got.flags & ivanna::OMEGA_FLAG_SUPREME_EBPF_BYPASS_ON, 0u);
    EXPECT_FLOAT_EQ(got.supreme_lattice_bl_drive, 1.45f);
    EXPECT_FLOAT_EQ(got.supreme_lattice_lambda, 0.756f);
    EXPECT_FLOAT_EQ(got.supreme_cvnn_harmonic_gain, 0.62f);
    EXPECT_FLOAT_EQ(got.supreme_cvnn_imd_cancel, 0.88f);
    EXPECT_FLOAT_EQ(got.supreme_snn_immersivity, 0.79f);
    EXPECT_FLOAT_EQ(got.supreme_snn_spike_threshold, 0.39f);
    EXPECT_FLOAT_EQ(got.supreme_pinna_wet_mix, 0.71f);
    EXPECT_FLOAT_EQ(got.supreme_pinna_concha_depth, 0.21f);
    EXPECT_FLOAT_EQ(got.supreme_pinna_helix_curl, -0.09f);
    EXPECT_FLOAT_EQ(got.supreme_pinna_head_width, 0.14f);
    EXPECT_FLOAT_EQ(got.supreme_mso_itd_ns, 6400.0f);

    reader.close();
    writer.close();
    ::unlink(kTestPath3);
}
