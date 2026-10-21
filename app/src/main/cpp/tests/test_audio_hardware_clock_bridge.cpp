#include "include/audio_hardware_clock_bridge.hpp"

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

} // namespace
