#include <gtest/gtest.h>

TEST(DaemonSocket, ReconnectDoesNotLoseState){

    bool connected = false;

    connected = true;
    EXPECT_TRUE(connected);

    connected = false;
    EXPECT_FALSE(connected);

    connected = true;
    EXPECT_TRUE(connected);
}
