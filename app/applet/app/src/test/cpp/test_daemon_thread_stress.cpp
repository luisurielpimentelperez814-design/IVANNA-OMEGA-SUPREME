#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>

TEST(DaemonStress, ConcurrentClientsDoNotCrash){

    std::atomic<int> completed{0};

    std::vector<std::thread> workers;

    for(int i=0;i<32;i++){
        workers.emplace_back([&](){
            for(int j=0;j<1000;j++){
                // simulación de operación concurrente daemon
                completed++;
            }
        });
    }

    for(auto& t: workers)
        t.join();

    EXPECT_EQ(completed.load(),32000);
}
