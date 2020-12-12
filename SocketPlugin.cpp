#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>

#include "SocketPlugin.h"
#include "utils.h"


int SocketPlugin::connectSocket(const char *addrStr, int port) {

    
    struct sockaddr_in6 dest6;
    memset(&dest6, 0, sizeof(dest6));
    dest6.sin6_family = AF_INET6;
    dest6.sin6_port = htons(port);

    struct sockaddr_in dest4;
    memset(&dest4, 0, sizeof(dest4));
    dest4.sin_family = AF_INET;
    dest4.sin_port = htons(port);
    LogHelper::log(Info, "server address %s", addrStr);
    int convert6Code = inet_pton(AF_INET6, addrStr, &dest6.sin6_addr);
    struct sockaddr* destPtr;
    int destSize = 0;
    int useIpv6 = false;

    if(convert6Code == 0) {
        // dest4.sin_addr.s_addr = inet_addr(addrStr);
        // int convert4Code = inet_pton(AF_INET, addrStr, &dest4.sin_addr);
        destPtr = (struct sockaddr*) &dest4;
        destSize = sizeof(dest4);
        if( inet_pton(AF_INET, addrStr, &dest4.sin_addr) != 1) {
            LogHelper::log(Error, "Fail to convert ipv4 Address, %s", strerror(errno));
            return -1;
        }
        // else {
        //     destPtr = (struct sockaddr*) &dest4;
        //     destSize = sizeof(dest4);
        // }
    }
    else if (convert6Code != 1) {
        LogHelper::log(Error, "Fail to convert ipv6 Address, %s", strerror(errno));
        return -1;
    }
    else {
        useIpv6 = true;
        destPtr = (struct sockaddr*) &dest6;
        destSize = sizeof(dest6);
    }

    int netFamily = AF_INET;
    if (useIpv6)
        netFamily = AF_INET6;
    int socketFd = 0;
    LogHelper::log(Debug, "useIpv6: %d, netFamily: %d, AF_INET: %d, AF_INET6: %d", useIpv6, netFamily, AF_INET, AF_INET6);
    if((socketFd = socket(netFamily, SOCK_STREAM, 0)) < 0) {
        LogHelper::log(Error, "Fail to create socket, %s", strerror(errno));
        return -1;
    }
    
    LogHelper::log(Info, "before connect socket");
    if(connect(socketFd, (struct sockaddr*) destPtr, destSize) < 0) {
        LogHelper::log(Error, "Fail to connect socket, %s", strerror(errno));
        return -1;
    }
    LogHelper::log(Info, "connect succeed");
    this->m_sockfd = socketFd;
    return 0;
}

int SocketPlugin::closeSocket() {

    int ret = close(this->m_sockfd);
    if(ret == -1) {
        LogHelper::log(Error, "close socket failed", strerror(errno));
    }
    LogHelper::log(Info, "close socket");
    return ret;
}

int SocketPlugin::getSockFd() {
    return this->m_sockfd;
}

int SocketPlugin::writeMsg(const void *msg, size_t msgLen) {
//    size_t hasWritten = 0;
//    const char* dest = (const char*)msg;
//    while(hasWritten < msgLen) {
//        ssize_t tempLen = write(m_sockfd, dest + hasWritten, msgLen - hasWritten);
//        if(tempLen < 0) {
//            LogHelper::log(Error, "Errors when write socket, %s", strerror(errno));
//            return -1;
//        }
//        hasWritten += tempLen;
//    }
//    return 0;
    return writeNBytes(this->m_sockfd, msg, msgLen);
}

int SocketPlugin::readMsg(void *buf, size_t msgLen) {
//    size_t hasRead = 0;
//    char* src = (char*)buf;
//    while(hasRead < msgLen) {
//        ssize_t tempLen = read(m_sockfd, src + hasRead, msgLen - hasRead);
//        if(tempLen < 0) {
//            LogHelper::log(Error, "Error happens when read socket, %s", strerror(errno));
//            return -1;
//        }
//        hasRead += tempLen;
//    }
//    return 0;
    return readNBytes(this->m_sockfd, buf, msgLen);
}