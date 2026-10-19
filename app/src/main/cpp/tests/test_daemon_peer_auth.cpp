#include <gtest/gtest.h>
#include "daemon/control/peer_auth.hpp"

TEST(DaemonPeerAuth, AllowsOnlyRootShellAndExactInstalledAppUid) {
    const std::vector<uid_t> appUids{10123, 110123};
    EXPECT_TRUE(ivanna::security::isAuthorizedControlUid(0, appUids));
    EXPECT_TRUE(ivanna::security::isAuthorizedControlUid(2000, appUids));
    EXPECT_TRUE(ivanna::security::isAuthorizedControlUid(10123, appUids));
    EXPECT_TRUE(ivanna::security::isAuthorizedControlUid(110123, appUids));
    EXPECT_FALSE(ivanna::security::isAuthorizedControlUid(10124, appUids));
    EXPECT_FALSE(ivanna::security::isAuthorizedControlUid(210123, appUids));
}

TEST(DaemonPeerAuth, DeniesUnknownUidWhenPackageIsNotInstalled) {
    EXPECT_FALSE(ivanna::security::isAuthorizedControlUid(10123, {}));
    EXPECT_FALSE(ivanna::security::isAuthorizedControlUid(1000, {}));
}
