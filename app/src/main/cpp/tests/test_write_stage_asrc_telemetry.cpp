// Pruebas de host del relevo del dump del write-stage ASRC PAL (v5)
// (include/write_stage_asrc_telemetry.hpp): el daemon lee
// /data/vendor/ivanna_audio/pal_asrc_stage.txt y lo releva en
// GET_CLOCK_TELEMETRY para que la app lo muestre.
//
// Alcance honesto: valida el parser y la frescura del archivo, NO el
// write-stage en el HAL vivo ni el daemon completo (eso requiere el
// teléfono: ver analysis-ivanna/PLAN_VERIFICACION_ASRC_384.md). Los
// valores de los fixtures son líneas reales del formato que escribe
// dump_once() en ivanna_asrc_write_stage.c (v5).

#include "../include/write_stage_asrc_telemetry.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

namespace {

using ivanna::audio::WriteStageAsrcSample;
using ivanna::audio::parseWriteStageAsrcDumpLine;
using ivanna::audio::readWriteStageAsrc;

// Línea real del dump v5 (formato de dump_once(), 2026-11-10 04:40 CST:
// ratio=1.000058055 drift +58.055 ppm clock=write-timing opens=8 ok=8).
constexpr const char* kRealLine =
    "IVANNA ASRC WRITE STAGE v5 pid=9123 enabled=1 ratio=1.000058055 "
    "drift_ppm=58.055 clock=write-timing mmap_ok=0 mmap_fail=0 mmap_resets=0 "
    "opens=8 ok=8 fail=0 closes=0 open_no_real=0 close_no_real=0 "
    "write_no_real=0 bad_attr=0 no_handle=0 table_full=0 pt_no_slot=0 "
    "pt_fmt=0 pt_size=0 pt_off=0 pt_nobuf=0 pt_big=0 max_write_frames=4096 "
    "sat=0 sat_ticks=0 sat_entries=0 rej=0 hold=0 kalman_drift=58.055 "
    "kalman_rate=0.000 kalman_var=400.0 kalman_r=4000.0 kalman_upd=120 "
    "kalman_gated=0 fir_taps=287 dither=1 neon=1 up192=0";

std::string tmpPath() {
    return std::string("/tmp/ivanna_ws_asrc_") + std::to_string(::getpid());
}

class WriteStageAsrcTelemetryTest : public ::testing::Test {
protected:
    void TearDown() override { ::unlink(path_.c_str()); }
    std::string path_ = tmpPath();

    bool writeFile(const std::string& content) {
        FILE* f = std::fopen(path_.c_str(), "w");
        if (!f) return false;
        const bool ok = std::fwrite(content.data(), 1, content.size(), f) == content.size();
        std::fclose(f);
        return ok;
    }
};

TEST_F(WriteStageAsrcTelemetryTest, RealV5LineParses) {
    WriteStageAsrcSample s;
    ASSERT_TRUE(parseWriteStageAsrcDumpLine(kRealLine, 4000, s));
    EXPECT_TRUE(s.valid);
    EXPECT_EQ(s.pid, 9123);
    EXPECT_TRUE(s.enabled);
    EXPECT_NEAR(s.ratio, 1.000058055, 1e-9);
    EXPECT_NEAR(s.driftPpm, 58.055, 1e-3);
    EXPECT_NEAR(s.kalmanDriftPpm, 58.055, 1e-3);
    EXPECT_STREQ(s.clock, "write-timing");
    EXPECT_EQ(s.opens, 8u);
    EXPECT_EQ(s.ok, 8u);
    EXPECT_EQ(s.sat, 0);
    EXPECT_EQ(s.firTaps, 287);
    EXPECT_EQ(s.neon, 1);
    EXPECT_EQ(s.ageMs, 4000u);
}

TEST_F(WriteStageAsrcTelemetryTest, MmapClockAndDisabledParse) {
    std::string line = kRealLine;
    const std::string needle = "clock=write-timing";
    line.replace(line.find(needle), needle.size(), "clock=mmap");
    line.replace(line.find("enabled=1"), 9, "enabled=0");
    line.replace(line.find("drift_ppm=58.055"), 16, "drift_ppm=-6.250");
    line.replace(line.find("ratio=1.000058055"), 17, "ratio=0.999993750");
    WriteStageAsrcSample s;
    ASSERT_TRUE(parseWriteStageAsrcDumpLine(line.c_str(), 1000, s));
    EXPECT_STREQ(s.clock, "mmap");
    EXPECT_FALSE(s.enabled);
    EXPECT_NEAR(s.driftPpm, -6.25, 1e-3);
    EXPECT_NEAR(s.ratio, 0.99999375, 1e-9);
}

TEST_F(WriteStageAsrcTelemetryTest, WrongHeaderRejected) {
    std::string line = kRealLine;
    line.replace(0, 6, "XXXXXX");
    WriteStageAsrcSample s;
    EXPECT_FALSE(parseWriteStageAsrcDumpLine(line.c_str(), 1000, s));
    EXPECT_FALSE(s.valid);
}

TEST_F(WriteStageAsrcTelemetryTest, V4HeaderRejected) {
    std::string line = kRealLine;
    line.replace(line.find("v5"), 2, "v4");
    WriteStageAsrcSample s;
    EXPECT_FALSE(parseWriteStageAsrcDumpLine(line.c_str(), 1000, s));
}

TEST_F(WriteStageAsrcTelemetryTest, RatioOutOfClampRejected) {
    std::string line = kRealLine;
    line.replace(line.find("ratio=1.000058055"), 17, "ratio=1.002000000");
    WriteStageAsrcSample s;
    EXPECT_FALSE(parseWriteStageAsrcDumpLine(line.c_str(), 1000, s));
}

TEST_F(WriteStageAsrcTelemetryTest, WrongFirTapsRejected) {
    std::string line = kRealLine;
    line.replace(line.find("fir_taps=287"), 12, "fir_taps=256");
    WriteStageAsrcSample s;
    EXPECT_FALSE(parseWriteStageAsrcDumpLine(line.c_str(), 1000, s));
}

TEST_F(WriteStageAsrcTelemetryTest, StaleDumpRejected) {
    WriteStageAsrcSample s;
    EXPECT_FALSE(parseWriteStageAsrcDumpLine(kRealLine, 30001, s));
    EXPECT_TRUE(parseWriteStageAsrcDumpLine(kRealLine, 30000, s));
}

TEST_F(WriteStageAsrcTelemetryTest, ReadFreshFile) {
    ASSERT_TRUE(writeFile(std::string(kRealLine) + "\nSTREAM h=0x1 fmt=48000/16/ch2/fmt1 src=attr eligible=1 writes=10 bytes=100 processed=10 passthrough=0 misalign=0 in_frames=100 out_frames=100 pend=0\n"));
    WriteStageAsrcSample s;
    ASSERT_TRUE(readWriteStageAsrc(s, path_.c_str()));
    EXPECT_TRUE(s.valid);
    EXPECT_EQ(s.pid, 9123);
    EXPECT_EQ(s.opens, 8u);
}

TEST_F(WriteStageAsrcTelemetryTest, ReadStaleFileRejected) {
    ASSERT_TRUE(writeFile(std::string(kRealLine) + "\n"));
    struct utimbuf old {};
    old.actime = 0;
    old.modtime = 0;  // 1970: muy viejo
    ASSERT_EQ(::utime(path_.c_str(), &old), 0);
    WriteStageAsrcSample s;
    EXPECT_FALSE(readWriteStageAsrc(s, path_.c_str()));
    EXPECT_FALSE(s.valid);
}

TEST_F(WriteStageAsrcTelemetryTest, MissingFileRejected) {
    WriteStageAsrcSample s;
    EXPECT_FALSE(readWriteStageAsrc(s, "/tmp/ivanna_ws_asrc_no_existe_12345.txt"));
    EXPECT_FALSE(s.valid);
}

TEST_F(WriteStageAsrcTelemetryTest, EmptyLineRejected) {
    WriteStageAsrcSample s;
    EXPECT_FALSE(parseWriteStageAsrcDumpLine("", 1000, s));
    EXPECT_FALSE(parseWriteStageAsrcDumpLine(nullptr, 1000, s));
}

} // namespace
