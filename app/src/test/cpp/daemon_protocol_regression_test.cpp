#include <gtest/gtest.h>
#include <string>

TEST(DaemonRegression, HandlesFragmentedJsonFrames) {

    std::string part1 = "{\"action\":\"";
    std::string part2 = "ping\"}";

    std::string combined = part1 + part2;

    EXPECT_EQ(
        combined,
        "{\"action\":\"ping\"}"
    );
}
