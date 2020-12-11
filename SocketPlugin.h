#ifndef SOCKET_PLUGIN_H
#define SOCKET_PLUGIN_H

#include "Singleton.h"

class SocketPlugin: public Singleton<SocketPlugin> {
private:
    friend Singleton<SocketPlugin>;
    int m_sockfd;



    SocketPlugin() {};


public:

    // now default interpret addr as ipv6 addr
    int connectSocket(const char* addrStr, int port);
    int getSockFd();
    int closeSocket();

    int writeMsg(const void* msg, size_t msgLen);
    int readMsg(void* buf, size_t msgLen);
    ~SocketPlugin() {};

};

#endif