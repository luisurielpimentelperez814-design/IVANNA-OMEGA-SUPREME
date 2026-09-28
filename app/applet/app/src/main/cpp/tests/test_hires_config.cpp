// test_hires_config.cpp — regresión del contrato hi-res app<->daemon.
#include "ivanna_hires_config.hpp"
#include <gtest/gtest.h>
#include <cstdio>
#include <string>
static std::string tmpPath(const char* n){const char* t=std::getenv("TMPDIR");return std::string(t&&*t?t:"/tmp")+"/"+n;}
TEST(HiResConfig, RatesCerrados){
  // Ambas familias aceptadas: 48k (video/Android) y 44.1k (CD/streaming).
  // La familia 44.1k se anadio el 2026-09-20 — rechazarla forzaba un
  // remuestreo 147:160 permanente sobre practicamente toda la musica.
  for(int r:{44100,48000,88200,96000,176400,192000,352800,384000})
    EXPECT_TRUE(ivanna::hires::isValidRate(r)) << "rate legitimo rechazado: " << r;
  for(int r:{0,22050,32000,64000,768000,-1,-44100})
    EXPECT_FALSE(ivanna::hires::isValidRate(r)) << "rate invalido aceptado: " << r;
}

TEST(HiResConfig, RoundTripFamilia44k){
  const std::string pathStr = tmpPath("hires_44k.conf");
  const char* path = pathStr.c_str();
  ASSERT_TRUE(ivanna::hires::writeConf(path, 176400, 32));
  int r = 0, d = 0;
  ASSERT_TRUE(ivanna::hires::readConf(path, r, d));
  EXPECT_EQ(r, 176400);
  EXPECT_EQ(d, 32);
  std::remove(path);
}
TEST(HiResConfig, DepthsCerrados){for(int d:{16,24,32})EXPECT_TRUE(ivanna::hires::isValidDepth(d));for(int d:{0,8,20,64,-1})EXPECT_FALSE(ivanna::hires::isValidDepth(d));}
TEST(HiResConfig, RoundTrip){const std::string p=tmpPath("hires_conf_test.conf");ASSERT_TRUE(ivanna::hires::writeConf(p.c_str(),384000,32));int r=0,d=0;ASSERT_TRUE(ivanna::hires::readConf(p.c_str(),r,d));EXPECT_EQ(r,384000);EXPECT_EQ(d,32);std::remove(p.c_str());}
TEST(HiResConfig, RechazaInvalidas){EXPECT_FALSE(ivanna::hires::writeConf("/tmp/x_hires.conf",22050,24));EXPECT_FALSE(ivanna::hires::writeConf("/tmp/x_hires.conf",96000,20));int r=0,d=0;EXPECT_FALSE(ivanna::hires::readConf("/tmp/no_existe_hires.conf",r,d));}
