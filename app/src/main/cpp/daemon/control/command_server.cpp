#include "command_server.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>


bool CommandServer::start(const std::string& socketName)
{
    serverFd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);

    if (serverFd < 0)
        return false;


    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;


    if (socketName[0] == '@')
    {
        addr.sun_path[0] = '\0';

        strncpy(
            addr.sun_path + 1,
            socketName.c_str() + 1,
            sizeof(addr.sun_path) - 2
        );
    }
    else
    {
        strncpy(
            addr.sun_path,
            socketName.c_str(),
            sizeof(addr.sun_path)-1
        );
    }


    if (bind(
        serverFd,
        reinterpret_cast<sockaddr*>(&addr),
        sizeof(addr)) < 0)
    {
        close(serverFd);
        serverFd = -1;
        return false;
    }


    return listen(serverFd, 8) == 0;
}


void CommandServer::stop()
{
    if(serverFd >= 0)
    {
        close(serverFd);
        serverFd = -1;
    }
}
