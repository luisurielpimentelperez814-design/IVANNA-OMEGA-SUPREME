#include <gtest/gtest.h>

TEST(AndroidLifecycle, StateTransitionsRemainValid){

    enum State {
        CREATED,
        STARTED,
        STOPPED
    };

    State state = CREATED;

    state = STARTED;
    EXPECT_EQ(state,STARTED);

    state = STOPPED;
    EXPECT_EQ(state,STOPPED);
}
