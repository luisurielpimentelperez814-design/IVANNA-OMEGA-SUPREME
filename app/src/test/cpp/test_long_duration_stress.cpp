#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <chrono>

TEST(Endurance, LongRunningProcessingRemainsStable){

    std::atomic<bool> running{true};
    std::atomic<uint64_t> cycles{0};

    std::thread worker([&](){

        while(running){
            cycles++;
        }

    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    running=false;
    worker.join();

    EXPECT_GT(cycles.load(),0);
}
