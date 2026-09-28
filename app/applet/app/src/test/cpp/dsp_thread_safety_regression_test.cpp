#include <gtest/gtest.h>
#include <thread>
#include <atomic>

TEST(DSPRegression, ConcurrentParameterAccessSafe) {

    std::atomic<float> parameter{0.0f};

    std::thread audio([&](){
        for(int i=0;i<10000;i++)
            parameter.load();
    });

    std::thread control([&](){
        for(int i=0;i<10000;i++)
            parameter.store(i * 0.001f);
    });

    audio.join();
    control.join();

    EXPECT_GE(parameter.load(),0.0f);
}
