#include <gtest/gtest.h>
#include <thread>
#include <atomic>

TEST(CommandBus, ParallelCommandsRemainConsistent){

    std::atomic<int> counter{0};

    auto worker=[&](){
        for(int i=0;i<10000;i++)
            counter.fetch_add(1,std::memory_order_relaxed);
    };

    std::thread a(worker);
    std::thread b(worker);
    std::thread c(worker);
    std::thread d(worker);

    a.join();
    b.join();
    c.join();
    d.join();

    EXPECT_EQ(counter.load(),40000);
}
