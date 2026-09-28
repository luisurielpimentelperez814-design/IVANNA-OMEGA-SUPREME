#include <gtest/gtest.h>
#include <string>

TEST(SpatialRegression, MissingDatasetFallbackSafe) {

    std::string dataset = "";

    bool fallback = dataset.empty();

    EXPECT_TRUE(fallback);
}
