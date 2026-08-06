#pragma once

#include <string>

class CommandServer {
public:
    bool start(const std::string& socketName);
    void stop();

private:
    int serverFd = -1;
};
