#include "include/audio_hardware_clock_bridge.hpp"
#include "include/audio_hardware_clock_hal_adapter.hpp"
#include "../../../../../hal_integration/ivanna_audio_clock_reporter.hpp"

#include <gtest/gtest.h>
#include <cstdio>
#include <string>
#include <unistd.h>

namespace {

std::string testPath() {
    return "/tmp/ivanna_hw_clock_snapshot_test_" + std::to_string(::getpid());
}

TEST(HardwareClockBridge, PublishesAndReadsDriverSnapshot) {
    const std::string path = testPath();
    std::remove(path.c_str());
    ivanna::audio::HardwareClockPublisher publisher;
    ivanna::audio::HardwareClockReader reader;
    ASSERT_TRUE(publisher.open(path.c_str()));
    ASSERT_TRUE(reader.open(path.c_str()));

    const ivanna::audio::HardwareClockSample sent{96000, 3000000000ull, 48000};
    ASSERT_TRUE(publisher.publish(sent));
    ivanna::audio::HardwareClockSample received;
    ASSERT_TRUE(reader.read(received));
    EXPECT_EQ(received.presentedFrames, sent.presentedFrames);
    EXPECT_EQ(received.hardwareTimestampNs, sent.hardwareTimestampNs);
    EXPECT_EQ(received.nominalSampleRateHz, sent.nominalSampleRateHz);
    reader.close();
    publisher.close();
    std::remove(path.c_str());
}

TEST(HardwareClockBridge, HalAdapterConvertsAssociatedTimespec) {
    const std::string path = testPath();
    std::remove(path.c_str());
    ivanna::audio::HardwareClockPublisher publisher;
    ASSERT_TRUE(publisher.open(path.c_str()));
    ASSERT_TRUE(ivanna::audio::publishHalPresentationPosition(
        publisher, 48000, 2, 500000000, 48000));
    ivanna::audio::HardwareClockReader reader;
    ASSERT_TRUE(reader.open(path.c_str()));
    ivanna::audio::HardwareClockSample sample;
    ASSERT_TRUE(reader.read(sample));
    EXPECT_EQ(sample.hardwareTimestampNs, 2500000000ull);
    EXPECT_EQ(sample.presentedFrames, 48000ull);
    EXPECT_FALSE(ivanna::audio::publishHalPresentationPosition(
        publisher, 49000, 2, 1000000000, 48000));
    reader.close();
    publisher.close();
    std::remove(path.c_str());
}

TEST(HardwareClockBridge, RejectsInvalidSamples) {
    const std::string path = testPath();
    std::remove(path.c_str());
    ivanna::audio::HardwareClockPublisher publisher;
    ASSERT_TRUE(publisher.open(path.c_str()));
    EXPECT_FALSE(publisher.publish({10, 0, 48000}));
    EXPECT_FALSE(publisher.publish({10, 1000000, 1000}));
    publisher.close();
    std::remove(path.c_str());
}


TEST(HalOutputClockReporter, PublishesAssociatedFrameAndTimestamp) {
    const std::string path = testPath();
    std::remove(path.c_str());

    ivanna::audio::HalOutputClockReporter reporter;
    ASSERT_TRUE(reporter.start(path.c_str()));

    const timespec timestamp{2, 500000000};
    ASSERT_TRUE(reporter.report(48000, timestamp, 48000));

    ivanna::audio::HardwareClockReader reader;
    ASSERT_TRUE(reader.open(path.c_str()));
    ivanna::audio::HardwareClockSample sample;
    ASSERT_TRUE(reader.read(sample));
    EXPECT_EQ(sample.presentedFrames, 48000ull);
    EXPECT_EQ(sample.hardwareTimestampNs, 2500000000ull);
    EXPECT_EQ(sample.nominalSampleRateHz, 48000u);

    const timespec invalidTimestamp{2, 1000000000};
    EXPECT_FALSE(reporter.report(49000, invalidTimestamp, 48000));

    reader.close();
    reporter.stop();
    std::remove(path.c_str());
}

} // namespace

#include "include/audio_hardware_clock_rate_servo.hpp"

TEST(HardwareClockRateServo, RequiresThreeValidSamplesAndTracksDrift) {
    ivanna::audio::HardwareClockRateServo servo;
    const ivanna::audio::HardwareClockStatus drift{true,48000,1000000000ull,48004.8,100.0};
    EXPECT_FALSE(servo.observe(drift).enabled);
    EXPECT_FALSE(servo.observe(drift).enabled);
    auto output=servo.observe(drift);
    ASSERT_TRUE(output.enabled);
    for(int i=0;i<40;++i) output=servo.observe(drift);
    ASSERT_TRUE(output.enabled);
    EXPECT_GT(output.correctionPpm,90.0);
    EXPECT_LE(output.correctionPpm,100.0);
    EXPECT_LT(output.inputFramesPerOutputFrame,1.0);
    EXPECT_GT(output.inputFramesPerOutputFrame,0.9995);
}
TEST(HardwareClockRateServo, SlewLimitsAndInvalidSamplesFailClosed) {
    ivanna::audio::HardwareClockRateServo servo;
    const ivanna::audio::HardwareClockStatus jump{true,48000,1000000000ull,48240.0,5000.0};
    EXPECT_FALSE(servo.observe(jump).enabled);
    EXPECT_FALSE(servo.observe(jump).enabled);
    EXPECT_FALSE(servo.observe(jump).enabled);
    const ivanna::audio::HardwareClockStatus drift{true,48000,1000000000ull,48024.0,500.0};
    servo.observe(drift); servo.observe(drift);
    auto output=servo.observe(drift);
    ASSERT_TRUE(output.enabled);
    EXPECT_LE(output.correctionPpm,25.0);
    EXPECT_GE(output.correctionPpm,0.0);
    const auto invalid=servo.observe({});
    EXPECT_FALSE(invalid.enabled);
    EXPECT_DOUBLE_EQ(invalid.inputFramesPerOutputFrame,1.0);
    EXPECT_FALSE(servo.current().enabled);
}
