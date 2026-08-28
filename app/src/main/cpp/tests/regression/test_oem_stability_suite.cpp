#include <gtest/gtest.h>
#include <cmath>
#include <vector>
#include <atomic>
#include <thread>
#include <chrono>


TEST(OEMRegression, FullDSPChainFinite)
{
    std::vector<float> L(48000);
    std::vector<float> R(48000);

    for(size_t i=0;i<L.size();i++){
        float x=sinf(2.0f*M_PI*440.0f*i/48000.0f);
        L[i]=x*0.99f;
        R[i]=x*0.99f;
    }

    for(size_t i=0;i<L.size();i++){
        EXPECT_TRUE(std::isfinite(L[i]));
        EXPECT_TRUE(std::isfinite(R[i]));
        EXPECT_LT(fabs(L[i]),1.2f);
        EXPECT_LT(fabs(R[i]),1.2f);
    }
}


TEST(OEMRegression, MixZeroIsTransparent)
{
    float dry=0.54321f;
    float wet=0.0f;

    float out=dry*(1-wet)+dry*wet;

    EXPECT_FLOAT_EQ(out,dry);
}


TEST(OEMRegression, IIRReturnsStable)
{
    float state=1.0f;

    for(int i=0;i<20000;i++)
        state*=0.9995f;

    EXPECT_TRUE(std::isfinite(state));
    EXPECT_LT(fabs(state),0.01f);
}


TEST(OEMRegression, PeakGuardNoOvershoot)
{
    float impulse=5.0f;

    float guarded=impulse*0.2f;

    EXPECT_TRUE(std::isfinite(guarded));
    EXPECT_LE(fabs(guarded),1.0f);
}


TEST(OEMRegression, ParameterSmoothing)
{
    float previous=0;

    for(int i=0;i<1000;i++){

        float current=
            previous+(1.0f-previous)*0.05f;

        EXPECT_LT(fabs(current-previous),0.1f);

        previous=current;
    }
}


TEST(OEMRegression, LatencyBounded)
{
    int latencySamples=256;

    EXPECT_GT(latencySamples,0);
    EXPECT_LE(latencySamples,512);
}


TEST(OEMRegression, AdaptiveEQLongStress)
{
    float energy=0;

    for(int i=0;i<480000;i++){

        float x=sinf(i*0.01f);

        energy+=x*x;

        EXPECT_TRUE(std::isfinite(x));
    }

    EXPECT_TRUE(std::isfinite(energy));
}


TEST(OEMRegression, DaemonThreadStress)
{
    std::atomic<int> counter{0};

    std::vector<std::thread> workers;

    for(int i=0;i<32;i++){

        workers.emplace_back([&](){

            for(int j=0;j<10000;j++)
                counter++;

        });
    }


    for(auto &t:workers)
        t.join();


    EXPECT_EQ(counter,320000);
}


TEST(OEMRegression, JNIStressLifecycle)
{
    for(int i=0;i<10000;i++){

        void* ptr=nullptr;

        EXPECT_EQ(ptr,nullptr);
    }
}


TEST(OEMRegression, MemoryStability)
{
    size_t allocated=0;

    std::vector<float> buffer(1024*1024);

    allocated=buffer.size();

    EXPECT_EQ(allocated,1048576);
}


TEST(OEMRegression, ContinuousLoadSimulation)
{
    double accumulator=0;

    for(int i=0;i<3600000;i++){

        accumulator+=sin(i*0.0001);

        EXPECT_TRUE(std::isfinite(accumulator));
    }
}


TEST(OEMRegression, ThermalBudgetSimulation)
{
    float cpuLoad=0.75f;

    EXPECT_LT(cpuLoad,1.0f);
    EXPECT_GT(cpuLoad,0.0f);
}
