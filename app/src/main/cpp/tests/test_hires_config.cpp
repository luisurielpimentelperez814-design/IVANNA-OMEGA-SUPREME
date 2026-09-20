// test_hires_config.cpp — regresión del contrato hi-res app<->daemon.
#include "ivanna_hires_config.hpp"
#include <gtest/gtest.h>
#include <cstdio>
#include <string>
static std::string tmpPath(const char* n){const char* t=std::getenv("TMPDIR");return std::string(t&&*t?t:"/tmp")+"/"+n;}
TEST(HiResConfig, RatesCerrados){for(int r:{48000,96000,192000,384000})EXPECT_TRUE(ivanna::hires::isValidRate(r));for(int r:{0,44100,88200,176400,352800,768000,-1})EXPECT_FALSE(ivanna::hires::isValidRate(r));}
TEST(HiResConfig, DepthsCerrados){for(int d:{16,24,32})EXPECT_TRUE(ivanna::hires::isValidDepth(d));for(int d:{0,8,20,64,-1})EXPECT_FALSE(ivanna::hires::isValidDepth(d));}
TEST(HiResConfig, RoundTrip){const std::string p=tmpPath("hires_conf_test.conf");ASSERT_TRUE(ivanna::hires::writeConf(p.c_str(),384000,32));int r=0,d=0;ASSERT_TRUE(ivanna::hires::readConf(p.c_str(),r,d));EXPECT_EQ(r,384000);EXPECT_EQ(d,32);std::remove(p.c_str());}
TEST(HiResConfig, RechazaInvalidas){EXPECT_FALSE(ivanna::hires::writeConf("/tmp/x_hires.conf",44100,24));EXPECT_FALSE(ivanna::hires::writeConf("/tmp/x_hires.conf",96000,20));int r=0,d=0;EXPECT_FALSE(ivanna::hires::readConf("/tmp/no_existe_hires.conf",r,d));}
