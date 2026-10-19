#pragma once

#include <algorithm>
#include <dirent.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

namespace ivanna::security {
inline bool isAuthorizedControlUid(uid_t peerUid, const std::vector<uid_t>& appUids) noexcept {
    if (peerUid == 0 || peerUid == 2000) return true; // root or adb shell
    return std::find(appUids.begin(), appUids.end(), peerUid) != appUids.end();
}

inline std::vector<uid_t> discoverIvannaAppUids() {
    std::vector<uid_t> result;
    DIR* users = opendir("/data/user");
    if (users) {
        while (dirent* entry = readdir(users)) {
            const std::string userId(entry->d_name);
            if (userId.empty() || !std::all_of(userId.begin(), userId.end(),
                    [](char c) { return c >= '0' && c <= '9'; })) continue;
            const std::string path = "/data/user/" + userId + "/com.ivanna.omega";
            struct stat st {};
            if (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) result.push_back(st.st_uid);
        }
        closedir(users);
    }
    struct stat legacy {};
    if (stat("/data/data/com.ivanna.omega", &legacy) == 0 && S_ISDIR(legacy.st_mode)) {
        result.push_back(legacy.st_uid);
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

inline bool isAuthorizedControlPeer(int fd) {
    struct PeerCredentials { pid_t pid; uid_t uid; gid_t gid; } peer {};
    socklen_t length = sizeof(peer);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &length) != 0 || length != sizeof(peer)) {
        return false;
    }
    return isAuthorizedControlUid(peer.uid, discoverIvannaAppUids());
}
} // namespace ivanna::security
