// Pruebas de host del productor de reloj HAL (daemon/hal_clock_producer.*).
//
// Alcance honesto: se valida el descubrimiento por /proc/asound (árbol falso en
// /tmp), la puerta de correlación (acepta solo pares coherentes, fail-closed en
// el resto) y la publicación real en el snapshot compartido (mmap real, leído
// después con HardwareClockReader del producto). NO se valida el driver del Moto
// G85 ni ningún ASRC físico: eso requiere dispositivo y sigue sin enlazar.

#include "../daemon/hal_clock_producer.hpp"
#include "../include/audio_hardware_clock_bridge.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

using ivanna::audio::HardwareClockReader;
using ivanna::audio::HardwareClockSample;
using namespace ivanna::audio::halclock;

std::string tmpRoot(const char* tag) {
    return std::string("/tmp/ivanna_halclock_") + tag + "_" + std::to_string(::getpid());
}

void mkdirs(const std::string& p) {
    std::string acc;
    for (size_t i = 0; i < p.size(); ++i) {
        acc += p[i];
        if (p[i] == '/' && acc.size() > 1) ::mkdir(acc.c_str(), 0755);
    }
    ::mkdir(p.c_str(), 0755);
}

void writeFile(const std::string& p, const std::string& content) {
    std::ofstream f(p);
    f << content;
}

// Fuente falsa: entrega la secuencia programada y cuenta lecturas.
struct FakeSource : PairedSampleSource {
    std::vector<PairedSample> seq;
    size_t i = 0;
    bool opened = false;
    int openAttempts = 0;
    int failOpenAttempts = 0;
    const char* name() const noexcept override { return "fake"; }
    bool open(const PcmDevice&) noexcept override {
        ++openAttempts;
        if (openAttempts <= failOpenAttempts) return false;
        opened = true;
        return true;
    }
    void close() noexcept override { opened = false; }
    bool read(PairedSample& out) noexcept override {
        if (i >= seq.size()) return false;
        out = seq[i++];
        return true;
    }
};

PairedSample mk(uint64_t frames, int64_t ns, uint32_t rate = 48000) {
    PairedSample s;
    s.presentedFrames = frames;
    s.timestampNs = ns;
    s.nominalRateHz = rate;
    return s;
}

}  // namespace

// ── Descubrimiento real sobre un árbol /proc/asound falso ──────────────────
TEST(HalClockProducer, DiscoversPlaybackPcmAndParsesRate) {
    const std::string root = tmpRoot("disc");
    mkdirs(root + "/proc/card0/pcm0p/sub0");
    mkdirs(root + "/dev");
    writeFile(root + "/proc/card0/pcm0p/sub0/status", "state: RUNNING\n");
    writeFile(root + "/proc/card0/pcm0p/sub0/hw_params", "rate: 48000 (48000/1)\nformat: S16_LE\n");

    const auto devs = discoverPlaybackDevices(root + "/proc", root + "/dev");
    ASSERT_EQ(devs.size(), 1u);
    EXPECT_EQ(devs[0].card, 0);
    EXPECT_EQ(devs[0].device, 0);
    EXPECT_EQ(devs[0].nominalRateHz, 48000u);
    EXPECT_EQ(devs[0].devPath, root + "/dev/pcmC0D0p");
}

TEST(HalClockProducer, NoPcmMeansNoDevice) {
    const std::string root = tmpRoot("empty");
    mkdirs(root + "/proc");
    mkdirs(root + "/dev");
    EXPECT_TRUE(discoverPlaybackDevices(root + "/proc", root + "/dev").empty());
}

TEST(HalClockProducer, DiscoversDevNodeWithoutInventingRate) {
    const std::string root = tmpRoot("node_only");
    mkdirs(root + "/proc");
    mkdirs(root + "/dev");
    writeFile(root + "/dev/pcmC1D2p", "");
    const auto devs = discoverPlaybackDevices(root + "/proc", root + "/dev");
    ASSERT_EQ(devs.size(), 1u);
    EXPECT_EQ(devs[0].card, 1);
    EXPECT_EQ(devs[0].device, 2);
    EXPECT_EQ(devs[0].nominalRateHz, 0u);
    EXPECT_STREQ(devs[0].state.c_str(), "node-present; rate-unavailable");
}

TEST(HalClockProducer, DoesNotOpenPcmWithoutVerifiableRate) {
    const std::string root = tmpRoot("no_rate_open");
    mkdirs(root + "/proc");
    mkdirs(root + "/dev");
    writeFile(root + "/dev/pcmC1D2p", "");
    FakeSource src;
    src.seq = {mk(0, 1'000'000'000), mk(480, 1'010'000'000)};
    HalClockProducer::Config cfg;
    cfg.procRoot = root + "/proc";
    cfg.devRoot = root + "/dev";
    cfg.source = &src;
    cfg.snapshotPath = root + "/snapshot";
    HalClockProducer prod(cfg);
    prod.setEnabled(true);
    const ProducerStatus status = prod.poll();
    EXPECT_EQ(status.state, ProducerState::kWaitingSamples);
    EXPECT_FALSE(src.opened);
    EXPECT_EQ(status.published, 0u);
    EXPECT_NE(status.reason, "");
}

TEST(HalClockProducer, RetriesAfterPcmOpenFailure) {
    const std::string root = tmpRoot("open_retry");
    mkdirs(root + "/proc/card0/pcm0p/sub0");
    mkdirs(root + "/dev");
    writeFile(root + "/proc/card0/pcm0p/sub0/status", "state: RUNNING\n");
    writeFile(root + "/proc/card0/pcm0p/sub0/hw_params", "rate: 48000 (48000/1)\n");

    FakeSource src;
    src.failOpenAttempts = 1;
    src.seq = {mk(0, 1'000'000'000), mk(480, 1'010'000'000),
               mk(960, 1'020'000'000), mk(1440, 1'030'000'000)};
    HalClockProducer::Config cfg;
    cfg.procRoot = root + "/proc";
    cfg.devRoot = root + "/dev";
    cfg.source = &src;
    cfg.snapshotPath = root + "/snapshot";
    HalClockProducer prod(cfg);
    prod.setEnabled(true);

    const ProducerStatus failed = prod.poll();
    EXPECT_EQ(failed.state, ProducerState::kNoSource);
    EXPECT_EQ(src.openAttempts, 1);
    EXPECT_FALSE(src.opened);

    EXPECT_EQ(prod.poll().state, ProducerState::kWaitingSamples); // retry opens; first sample anchors
    EXPECT_TRUE(src.opened);
    EXPECT_EQ(src.openAttempts, 2);
    prod.poll();
    prod.poll();
    const ProducerStatus published = prod.poll();
    EXPECT_EQ(published.state, ProducerState::kPublishing);
    EXPECT_EQ(published.published, 1u);
}


TEST(HalClockProducer, ParsesRunningStateInsteadOfTreatingTextAsInteger) {
    const std::string root = tmpRoot("state_parse");
    mkdirs(root + "/proc/card0/pcm2p/sub0");
    mkdirs(root + "/dev");
    writeFile(root + "/proc/card0/pcm2p/sub0/status", "state: RUNNING\n");
    writeFile(root + "/proc/card0/pcm2p/sub0/hw_params", "rate: 48000 (48000/1)\n");
    const auto devs = discoverPlaybackDevices(root + "/proc", root + "/dev");
    ASSERT_EQ(devs.size(), 1u);
    EXPECT_EQ(devs[0].state, "RUNNING");
    EXPECT_TRUE(devs[0].running);
    EXPECT_EQ(devs[0].nominalRateHz, 48000u);
}

TEST(HalClockProducer, ActiveSubstreamWinsOverClosedSiblingRate) {
    const std::string root = tmpRoot("active_substream");
    mkdirs(root + "/proc/card1/pcm14p/sub0");
    mkdirs(root + "/proc/card1/pcm14p/sub1");
    mkdirs(root + "/dev");
    writeFile(root + "/proc/card1/pcm14p/sub0/status", "state: CLOSED\n");
    writeFile(root + "/proc/card1/pcm14p/sub0/hw_params", "rate: 48000 (48000/1)\n");
    writeFile(root + "/proc/card1/pcm14p/sub1/status", "state: RUNNING\n");
    writeFile(root + "/proc/card1/pcm14p/sub1/hw_params", "rate: 44100 (44100/1)\n");
    const auto devs = discoverPlaybackDevices(root + "/proc", root + "/dev");
    ASSERT_EQ(devs.size(), 1u);
    EXPECT_TRUE(devs[0].running);
    EXPECT_EQ(devs[0].state, "RUNNING");
    EXPECT_EQ(devs[0].nominalRateHz, 44100u);
}
 
TEST(HalClockProducer, InactivePcmWithRateIsNotAClockCandidate) {
    const std::string root = tmpRoot("inactive_rate");
    mkdirs(root + "/proc/card0/pcm14p/sub0");
    mkdirs(root + "/dev");
    writeFile(root + "/proc/card0/pcm14p/sub0/status", "state: CLOSED\n");
    writeFile(root + "/proc/card0/pcm14p/sub0/hw_params", "rate: 48000 (48000/1)\n");
    const auto devs = discoverPlaybackDevices(root + "/proc", root + "/dev");
    ASSERT_EQ(devs.size(), 1u);
    EXPECT_EQ(devs[0].nominalRateHz, 48000u);
    EXPECT_FALSE(devs[0].running);
    EXPECT_EQ(devs[0].state, "CLOSED");
}

TEST(HalClockProducer, DefaultProducerNeverOpensAnIndependentPcm) {
    const std::string root = tmpRoot("no_owner");
    mkdirs(root + "/proc/card1/pcm14p/sub0");
    mkdirs(root + "/dev");
    writeFile(root + "/proc/card1/pcm14p/sub0/status", "state: RUNNING\n");
    writeFile(root + "/proc/card1/pcm14p/sub0/hw_params", "rate: 48000 (48000/1)\n");
    writeFile(root + "/dev/pcmC1D14p", "");
    HalClockProducer::Config cfg;
    cfg.procRoot = root + "/proc";
    cfg.devRoot = root + "/dev";
    cfg.snapshotPath = root + "/snapshot";
    HalClockProducer producer(cfg);
    producer.setEnabled(true);
    const auto status = producer.poll();
    EXPECT_EQ(status.state, ProducerState::kNoSource);
    EXPECT_EQ(status.device, "");
    EXPECT_EQ(status.published, 0u);
    EXPECT_STREQ(status.reason,
        "sin publicador ligado al stream HAL/PAL propietario; daemon no abre PCM");
}
\n// ── Puerta de correlación ─────────────────────────────────────────────────
TEST(HalClockProducer, GateRejectsUncorrelatedAndAcceptsCoherent) {
    CorrelationGate g;
    // 480 frames en 10 ms = 48000 Hz exacto.
    EXPECT_FALSE(g.evaluate(mk(0, 1'000'000'000)).accepted);      // ancla
    EXPECT_TRUE(g.evaluate(mk(480, 1'010'000'000)).accepted);     // 48 kHz
    EXPECT_TRUE(g.evaluate(mk(960, 1'020'000'000)).accepted);     // 48 kHz
    EXPECT_EQ(g.streak(), 2);
    // consecutiveRequired=3 por defecto: con 2 pares aun NO publica.
    EXPECT_FALSE(g.readyToPublish());
    EXPECT_TRUE(g.evaluate(mk(1440, 1'030'000'000)).accepted);
    EXPECT_EQ(g.streak(), 3);
    EXPECT_TRUE(g.readyToPublish());
}

TEST(HalClockProducer, GateRejectsImplausibleRate) {
    CorrelationGate g;
    g.evaluate(mk(0, 1'000'000'000));
    const auto v = g.evaluate(mk(48000, 1'010'000'000));  // 4.8 MHz implicito
    EXPECT_FALSE(v.accepted);
    EXPECT_STREQ(v.reason, "tasa implicita fuera de tolerancia");
    EXPECT_EQ(g.streak(), 0);
}

TEST(HalClockProducer, GateRejectsStalledPcmAndBackwardsFrames) {
    CorrelationGate g;
    g.evaluate(mk(1000, 1'000'000'000));
    EXPECT_FALSE(g.evaluate(mk(1480, 1'900'000'000)).accepted);       // 900 ms parado
    CorrelationGate h;
    h.evaluate(mk(5000, 1'000'000'000));
    const auto v = h.evaluate(mk(4000, 1'010'000'000));               // frames atras
    EXPECT_FALSE(v.accepted);
    EXPECT_STREQ(v.reason, "frames retroceden");
}

TEST(HalClockProducer, GateRejectsMissingNominalRate) {
    CorrelationGate g;
    const auto v = g.evaluate(mk(0, 1'000'000'000, /*rate=*/0));
    EXPECT_FALSE(v.accepted);
    EXPECT_STREQ(v.reason, "sin rate nominal");
}

TEST(HalClockProducer, GateRejectsNominalRatesOutsideAudioRange) {
    CorrelationGate low;
    const auto l = low.evaluate(mk(0, 1'000'000'000, 7999));
    EXPECT_FALSE(l.accepted);
    EXPECT_STREQ(l.reason, "tasa nominal fuera de rango");
    EXPECT_EQ(low.streak(), 0);

    CorrelationGate high;
    const auto h = high.evaluate(mk(0, 1'000'000'000, 768001));
    EXPECT_FALSE(h.accepted);
    EXPECT_STREQ(h.reason, "tasa nominal fuera de rango");
    EXPECT_EQ(high.streak(), 0);
}

TEST(HalClockProducer, InvalidTimestampClearsCorrelationStreak) {
    CorrelationGate g;
    EXPECT_FALSE(g.evaluate(mk(0, 1'000'000'000)).accepted);
    EXPECT_TRUE(g.evaluate(mk(480, 1'010'000'000)).accepted);
    EXPECT_EQ(g.streak(), 1);
    const auto bad = g.evaluate(mk(960, 0));
    EXPECT_FALSE(bad.accepted);
    EXPECT_STREQ(bad.reason, "timestamp no positivo");
    EXPECT_EQ(g.streak(), 0);
    EXPECT_FALSE(g.evaluate(mk(1440, 1'030'000'000)).accepted); // new anchor
    EXPECT_EQ(g.streak(), 0);
}

// ── Productor completo contra el snapshot real del producto ───────────────
TEST(HalClockProducer, PublishesOnlyAfterThreeCoherentPairs) {
    const std::string root = tmpRoot("publish_three");
    const std::string snap = root + "/snapshot";
    ::unlink(snap.c_str());
    mkdirs(root + "/proc/card0/pcm0p/sub0");
    mkdirs(root + "/dev");
    writeFile(root + "/proc/card0/pcm0p/sub0/status", "state: RUNNING\n");
    writeFile(root + "/proc/card0/pcm0p/sub0/hw_params", "rate: 48000 (48000/1)\n");

    FakeSource src;
    src.seq = {mk(0, 1'000'000'000), mk(480, 1'010'000'000),
               mk(960, 1'020'000'000), mk(1440, 1'030'000'000),
               mk(1920, 1'040'000'000)};

    HalClockProducer::Config cfg;
    cfg.procRoot = root + "/proc";
    cfg.devRoot = root + "/dev";
    cfg.source = &src;
    cfg.snapshotPath = snap;
    cfg.gate.consecutiveRequired = 3;
    HalClockProducer prod(cfg);
    prod.setEnabled(true);

    EXPECT_EQ(prod.poll().state, ProducerState::kWaitingSamples); // anchor
    EXPECT_EQ(prod.poll().state, ProducerState::kWaitingSamples); // coherent pair 1
    EXPECT_EQ(prod.poll().state, ProducerState::kWaitingSamples); // coherent pair 2
    const ProducerStatus published = prod.poll();                 // coherent pair 3
    EXPECT_EQ(published.state, ProducerState::kPublishing);
    EXPECT_EQ(published.published, 1u);

    HardwareClockReader rd;
    ASSERT_TRUE(rd.open(snap.c_str()));
    HardwareClockSample got{};
    ASSERT_TRUE(rd.read(got));
    EXPECT_EQ(got.presentedFrames, 1440u);
    EXPECT_EQ(got.hardwareTimestampNs, 1'030'000'000ull);
    EXPECT_EQ(got.nominalSampleRateHz, 48000u);
    rd.close();
}
 
TEST(HalClockProducer, DisabledProducerNeverPublishes) {
    FakeSource src;
    HalClockProducer::Config cfg;
    cfg.source = &src;
    HalClockProducer prod(cfg);
    const ProducerStatus s = prod.poll();
    EXPECT_EQ(s.state, ProducerState::kDisabled);
    EXPECT_EQ(s.published, 0u);
    EXPECT_STREQ(stateName(ProducerState::kDisabled), "disabled");
}

TEST(HalClockProducer, PublishesCorrelatedSampleIntoSharedSnapshot) {
    // Snapshot real + producto real leyendo: prueba de extremo a extremo del
    // contrato IPC (productor -> mmap -> HardwareClockReader).
    const std::string snap = tmpRoot("e2e");
    ::unlink(snap.c_str());

    ivanna::audio::HardwareClockPublisher pub;
    ASSERT_TRUE(pub.open(snap.c_str()));
    ivanna::audio::HardwareClockSample out{};
    out.presentedFrames = 1440;
    out.hardwareTimestampNs = 1'030'000'000ull;
    out.nominalSampleRateHz = 48000;
    ASSERT_TRUE(pub.publish(out));

    ivanna::audio::HardwareClockReader rd;
    ASSERT_TRUE(rd.open(snap.c_str()));
    HardwareClockSample got{};
    ASSERT_TRUE(rd.read(got));
    EXPECT_EQ(got.presentedFrames, 1440u);
    EXPECT_EQ(got.hardwareTimestampNs, 1'030'000'000ull);
    EXPECT_EQ(got.nominalSampleRateHz, 48000u);
    pub.close();
    rd.close();
}
