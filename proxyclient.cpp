#include "proxyclient.h"
#include "SocketPlugin.h"
#include "constants.h"
#include "utils.h"

#include <chrono>
#include <thread>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <unistd.h>
#include <assert.h>
#include <signal.h>

#include <unordered_map>


using namespace constants;

struct ClientContext {
    uint64_t cSeq, sSeq;
    ClientContext(uint64_t cSeq, uint64_t sSeq): cSeq(cSeq), sSeq(sSeq) {}
    ClientContext(): cSeq(0), sSeq(0) {}

};

typedef std::unordered_map<int, int> LocalFdMap; 

enum LocalFdStatus{
    LocalFdUninitialized = 0,
    LocalFdWaiting = 1,
    LocalFdPrepared = 2
};

ssize_t clientHandleShake(ClientContext *ctx) {
    srand(time(NULL));
    uint64_t cSeq = getRand63();
    static InnerMsg innerMsg;
    memset(&innerMsg, 0, sizeof(innerMsg));
    innerMsg.cSeq = cSeq;
    innerMsg.sSeq = 0;
    innerMsg.msgType = 1;
    innerMsg.dataLength = 0;
    ssize_t sendCode = sendTSPacket(SocketPlugin::getInstance()->getSockFd(), &innerMsg, constants::clientNonBlocking);
    if (sendCode < 0) {
        LogHelper::log(Error, "client handle shake failed, fail to send first packet");
        return sendCode;
    }
    memset(&innerMsg, 0, sizeof(innerMsg));
    ssize_t recvCode = recvTSPacket(SocketPlugin::getInstance()->getSockFd(), &innerMsg, constants::clientNonBlocking);
    if (recvCode < 0) {
        LogHelper::log(Error, "client handle failed, fail to recv second packet");
    }
    if (innerMsg.cSeq != cSeq) {
        LogHelper::log(Error, "receive cSeq not inc, receive cSeq: %llu, expected: %llu", innerMsg.cSeq, cSeq);
        return -3;
    }
    if (innerMsg.msgType != 2) {
        LogHelper::log(Error, "client hand shake failed, msgType not equals to 2, recv Type: %d", innerMsg.msgType);
        return -4;
    }
    innerMsg.cSeq++;
    // innerMsg.sSeq;
    innerMsg.msgType = 3;
    innerMsg.dataLength = 0;
    sendCode = sendTSPacket(SocketPlugin::getInstance()->getSockFd(), &innerMsg, constants::clientNonBlocking);
    if (sendCode < 0) {
        LogHelper::log(Error, "client handle shake failed, fail to send third packet");
        return sendCode;
    }

    *ctx = ClientContext(innerMsg.cSeq, innerMsg.sSeq);
    return 0;
}



ssize_t sendMessage(int sockFd, const uint8_t *src, size_t nbyte, ClientContext *ctx, int msgType = 4) {
    static InnerMsg innerMsg;
    memset(&innerMsg, 0, sizeof(innerMsg));
    ctx->cSeq++;
    innerMsg.cSeq = ctx->cSeq;
    innerMsg.sSeq = ctx->sSeq;
    innerMsg.msgType = msgType;
    if (nbyte > sizeof(innerMsg.data)) {
        LogHelper::log(Error, "Failed to send data, nbyte too large, nbyte: %lu", nbyte);
        return -1;
    }
    innerMsg.dataLength = nbyte;
    memcpy(innerMsg.data, src, nbyte);
    ssize_t sendCode = sendTSPacket(sockFd, &innerMsg, constants::serverNonBlocking);
    if (sendCode < 0) {
        LogHelper::log(Error, "Fail to send data");
        return sendCode;
    }
    return 0;
}

ssize_t sendRequestMsg(int serverFd, int localFd, const uint8_t *src, size_t nbyte, ClientContext *ctx) {
    static uint8_t msgBuffer[constants::AES_MAX_DATA_LENGTH];
    memset(msgBuffer, 0, sizeof(msgBuffer));
    hton_copy4bytes(msgBuffer, &localFd);
    if (sizeof(msgBuffer) < nbyte + 4) {
        LogHelper::log(Warn, "Failed to send request msg, nbyte greater than buffersize + 4");
        return -1;
    }
    memcpy(msgBuffer + 4, src, nbyte);
    ssize_t sendCode = sendMessage(serverFd, msgBuffer, nbyte + 4, ctx, 8);
    if (sendCode < 0) {
        LogHelper::log(Error, "Failed to send request msg");
        return sendCode;
    }
    return 0;
}

ssize_t recvMessage(int sockFd, uint8_t *dst, size_t bufferSize, ClientContext *ctx, uint8_t *msgTypePtr = NULL) {
    static InnerMsg innerMsg;
    memset(&innerMsg, 0, sizeof(innerMsg));

    ssize_t recvCode = recvTSPacket(sockFd, &innerMsg, constants::serverNonBlocking);
    if (recvCode < 0) {
        LogHelper::log(Error, "Fail to recv data");
        return recvCode;
    }
    if (innerMsg.sSeq != ctx->sSeq + 1) {
        LogHelper::log(Error, "Fail to recv data, seq error, sSeq: %llu, expected: %llu", innerMsg.sSeq, ctx->sSeq + 1);
        return -2;
    }
    if (innerMsg.msgType != 5 && innerMsg.msgType != 7 && innerMsg.msgType != 9) {
        LogHelper::log(Error, "Fail to recv data, msgType error, recv type :%d", innerMsg.msgType);
        return -3;
    }
    if (msgTypePtr != NULL)
        *msgTypePtr = innerMsg.msgType;
    if (innerMsg.dataLength > bufferSize) {
        LogHelper::log(Error, "Fail to recv data, bufferSize not enought, bufferSize :%lu, expected: %lu", bufferSize, innerMsg.dataLength);
        return -4;
    }
    memcpy(dst, innerMsg.data, innerMsg.dataLength);
    ctx->sSeq = innerMsg.sSeq;
    return innerMsg.dataLength;
}

int recvReplyMsg(int serverFd, uint8_t *dst, size_t bufferSize, ClientContext *ctx, int *localFdPtr) {
    static uint8_t msgBuffer[constants::MAX_MSG_DATA_LENGTH];
    memset(msgBuffer, 0, sizeof(msgBuffer));
    uint8_t msgType = 0;
    ssize_t recvCode = recvMessage(serverFd, msgBuffer, sizeof(msgBuffer), ctx, &msgType);
    if (msgType != 9) {
        LogHelper::log(Error, "Failed in reply msg, error msg type, get :%d, expected: %d", msgType, 9);
        return -3;
    }
    if (recvCode < 0) {
        LogHelper::log(Error, "Failed to recv reply msg");
        return recvCode;
    }
    if (recvCode > bufferSize + 4) {
        LogHelper::log(Error, "buffer size less than recv msg len + 4, buffersize: %d, msgLen: %d", bufferSize, recvCode);
        return -1;
    }
    if (recvCode < 4) {
        LogHelper::log(Error, "recv msg len less than 4, recv len: %d", recvCode);
        return -2;
    }
    int localFd = 0;
    ntoh_copy4bytes(&localFd, msgBuffer);
    memcpy(dst, msgBuffer + 4, recvCode - 4);
    assert(localFdPtr != NULL);
    *localFdPtr = localFd;
    return recvCode - 4;
}

void loopSend(int sockFd, ClientContext *ctx) {
    static char sendBuf[] = "Hello Server!";
    while (true) {
        ssize_t sendCode = sendMessage(sockFd, (const uint8_t *)sendBuf, sizeof(sendBuf), ctx);
        if (sendCode < 0) {
            LogHelper::log(Error, "Fail to send msg to Server, break");
            break;
        }
        LogHelper::log(Debug, "Send hello to server");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void loopRead(int sockFd, ClientContext *ctx) {
    static char buf[65536];
    while (true) {
        memset(buf, 0, sizeof(buf));
        ssize_t recvCode = recvMessage(sockFd, (uint8_t *)buf, sizeof(buf), ctx, NULL);
        if (recvCode < 0) {
            LogHelper::log(Error, "Fail to recv msg from server, break");
            break;
        }
        LogHelper::log(Debug, "recv msg from server: %s", buf);
        // std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

int socksHandShake(int localFd, int serverFd, ClientContext *ctx, LocalFdMap &localFdMap) {
    LogHelper::log(Debug, "client start to socks handshake for localFd: %d", localFd);
    static uint8_t socksBuffer[sizeof(SocksTcpReply)];
    memset(socksBuffer, 0, sizeof(socksBuffer));
    int recvCode = readNBytes(localFd, socksBuffer, 2, constants::clientNonBlocking);
    if (recvCode < 0) {
        LogHelper::log(Warn, "Fail to socks handshake in reading header in the first step");
        return recvCode;
    }
    uint8_t socksVersion = socksBuffer[0];
    uint8_t methodsNum = socksBuffer[1];
    if (socksVersion != constants::SocksVersion) {
        LogHelper::log(Warn, "socks version mismatch, version: %d", socksVersion);
        return -1;
    }
    memset(socksBuffer, 0, 2);
    recvCode = readNBytes(localFd, socksBuffer, methodsNum, constants::clientNonBlocking);
    if (recvCode <= 0 || methodsNum != recvCode) {
        LogHelper::log(Warn, "Fail to socks handshake in reading methods");
        return recvCode;
    }
    uint8_t supportMethod = constants::SocksNoSupportMethod;
    for (int i = 0; i < methodsNum; ++i) {
        if (socksBuffer[i] == constants::SocksNoAuthMethod) {
            supportMethod = socksBuffer[i];
            break;
        }
    }
    if (supportMethod == constants::SocksNoSupportMethod) {
        LogHelper::log(Warn, "No supportMethod in socks");
        // return -2;
    }
    memset(socksBuffer, 0, methodsNum);
    socksBuffer[0] = constants::SocksVersion;
    socksBuffer[1] = supportMethod;
    int sendCode = writeNBytes(localFd, socksBuffer, 2, constants::clientNonBlocking);
    if (sendCode < 0) {
        LogHelper::log(Warn, "Failed to send second packet in socks handshake");
        return sendCode;
    }

    memset(socksBuffer, 0, sizeof(socksBuffer));
    recvCode = readNBytes(localFd, socksBuffer, 4, constants::clientNonBlocking);
    if (recvCode < 0) {
        LogHelper::log(Warn, "Fail to recv thrid socks handshake header");
        return recvCode;
    }
    socksVersion = socksBuffer[0];
    uint8_t requestCmd = socksBuffer[1];
    uint8_t reserved = socksBuffer[2];
    uint8_t dstAddrType = socksBuffer[3];
    
    memset(socksBuffer, 0, 4);
    size_t totalLen = 1;
    socksBuffer[0] = dstAddrType;
    if (dstAddrType == constants::SocksAddrDomainType) {
        recvCode = readNBytes(localFd, socksBuffer + 1, 1, constants::clientNonBlocking);
        if (recvCode < 0) {
            LogHelper::log(Warn, "Fail to recv domain addr length in the third handshake recv");
            return recvCode;
        }
        uint8_t domainLen = socksBuffer[1];
        totalLen += 1;
        // socksBuffer[0] = 0;
        recvCode = readNBytes(localFd, socksBuffer + 2, domainLen, constants::clientNonBlocking);
        if (recvCode < 0) {
            LogHelper::log(Warn, "Fail to recv full domain addr in the third handshake recv");
            return recvCode;
        }
        totalLen += recvCode;
        recvCode = readNBytes(localFd, socksBuffer + 2 + recvCode, 2, constants::clientNonBlocking);
        if (recvCode < 0) {
            LogHelper::log(Warn, "Fail to recv dst port in the third handshake recv");
            return recvCode;
        }
        totalLen += recvCode;
    }
    else if (dstAddrType == constants::SocksAddrIpv4Type) {
        recvCode = readNBytes(localFd, socksBuffer + 1, 4, constants::clientNonBlocking);
        if (recvCode < 0) {
            LogHelper::log(Warn, "Fail to recv ipv4 addr in the third handshake recv");
            return recvCode;
        }
        totalLen += recvCode;
        recvCode = readNBytes(localFd, socksBuffer + 1 + recvCode, 2, constants::clientNonBlocking);
        if (recvCode < 0) {
            LogHelper::log(Warn, "Fail to recv dst port in the third handshake recv");
            return recvCode;
        }
        totalLen += recvCode;
    }
    else if (dstAddrType == constants::SocksAddrIpv6Type) {
        recvCode = readNBytes(localFd, socksBuffer + 1, 16, constants::clientNonBlocking);
        if (recvCode < 0) {
            LogHelper::log(Warn, "Fail to recv ipv6 addr in the third handshake recv");
            return recvCode;
        }
        totalLen += recvCode;
        recvCode = readNBytes(localFd, socksBuffer + 1 + recvCode, 2, constants::clientNonBlocking);
        if (recvCode < 0) {
            LogHelper::log(Warn, "Fail to recv dst port in the third handshake recv");
            return recvCode;
        }
        totalLen += recvCode;
    }
    else {
        LogHelper::log(Warn, "Not valid addr type in the third handshake recv, type: %d", dstAddrType);
        return recvCode;
    }

    static uint8_t addrBuffer[constants::SocksDomainMaxLength + 2];
    memset(addrBuffer, 0, sizeof(addrBuffer));
    memcpy(addrBuffer, socksBuffer, totalLen);
    memset(socksBuffer, 0, totalLen);
    socksBuffer[0] = requestCmd;
    memcpy(socksBuffer + 1, addrBuffer, totalLen);
    hton_copy4bytes(socksBuffer + 1 + totalLen, &localFd);
    // memcpy(addrBuffer, socksBuffer, recvCode);
    // memset(socksBuffer, 0, recvCode);
    sendCode = sendMessage(serverFd, socksBuffer, totalLen + 1 + sizeof(localFd), ctx, 6);
    if (sendCode < 0) {
        LogHelper::log(Error, "Fail to send socks handshake third request to server");
        return sendCode;
    }
    localFdMap[localFd] = LocalFdWaiting;
    
    return 0;
}

int readAndHandleFromServer(int serverFd, LocalFdMap &localFdMap, ClientContext *ctx, fd_set &master_set, int& max_sd) {
    static char readBuf[constants::MAX_MSG_DATA_LENGTH];
    int tempLocalFd = 0;
    uint8_t msgType = 0;
    // ssize_t recvCode = recvReplyMsg(serverFd, (uint8_t*)readBuf, sizeof(readBuf), ctx, &tempLocalFd);
    ssize_t recvCode = recvMessage(serverFd, (uint8_t*)readBuf, sizeof(readBuf), ctx, &msgType);
    if (recvCode < 0) {
        LogHelper::log(Error, "fail to recv msg from the server");
        return recvCode;
    }
    if (msgType == SocksTrafficReplyMsg) {
        ntoh_copy4bytes(&tempLocalFd, readBuf);
        if (localFdMap.find(tempLocalFd) == localFdMap.end()) {
            LogHelper::log(Warn, "Failed to recv traffic reply, localFd :%d not in localFdMap", tempLocalFd);
            return -1;
        }
        if (localFdMap[tempLocalFd] != LocalFdPrepared) {
            LogHelper::log(Warn, "Failed to recv traffic reply, localFd status: %d, not prepared", localFdMap[tempLocalFd]);
            return -2;
        }
        ssize_t sendCode = writeNBytes(tempLocalFd, readBuf + sizeof(tempLocalFd), recvCode - sizeof(tempLocalFd), constants::clientNonBlocking);
        if (sendCode < 0) {
            LogHelper::log(Warn, "Fail to send data to localFd :%d, close it", tempLocalFd);
            close(tempLocalFd);
            FD_CLR(tempLocalFd, &master_set);
            if (tempLocalFd == max_sd) {
                while (FD_ISSET(max_sd, &master_set) == 0)
                    max_sd -= 1;
            }
            localFdMap.erase(tempLocalFd);
            return sendCode;
        }
    }
    else if (msgType == SocksTcpReplyMsg) {
        // static uint8_t socksBuffer[sizeof(SocksTcpReply)];
        // memset(socksBuffer, 0, totalLen + 1 + sizeof(localFd));
        // TODO: modify
        // int recvLen = recvMessage(serverFd, socksBuffer, sizeof(socksBuffer), ctx, 7);
        // if (recvLen < 0) {
        //     LogHelper::log(Error, "Fail to recv socks handshake forth reply from server");
        //     return recvLen;
        // }
        int recvLen = recvCode;
        if (recvLen < 2) {
            LogHelper::log(Error, "Received len less than header in socks handshake forth reply from server");
            return -3;
        }
        uint8_t respCode = readBuf[0];

        int localFd = 0;
        static uint8_t addrBuffer[constants::SocksDomainMaxLength + 2];
        memset(addrBuffer, 0, sizeof(addrBuffer));
        memcpy(addrBuffer, readBuf + 1, recvLen - 1 - sizeof(localFd));
        
        ntoh_copy4bytes(&localFd, readBuf + recvLen - sizeof(localFd));
        // if (localFd != localFd) {
        //     LogHelper::log(Error, "receive localFd not equals the localFd, server localFd:%d, localFd: %d", localFd, localFd);
        //     return -4;
        // }
        if (localFdMap.find(localFd) == localFdMap.end()) {
            LogHelper::log(Warn, "Failed in forth socks handshake from server, cannot find localFd: %d in localFdMap", localFd);
            return -4;
        }
        if (localFdMap[localFd] != LocalFdWaiting) {
            LogHelper::log(Warn, "Failed in forth socks handshake, localFd %d status is not waiting but %d", localFd, localFdMap[localFd]);
            return -5;
        }
        localFdMap[localFd] = LocalFdPrepared;
        memset(readBuf, 0, recvLen);
        readBuf[0] = constants::SocksVersion;
        readBuf[1] = respCode;
        readBuf[2] = 0x00;
        memcpy(readBuf + 3, addrBuffer, recvLen - 1 - sizeof(localFd));
        int sendCode = writeNBytes(localFd, readBuf, 3 + recvLen - 1 - sizeof(localFd), constants::clientNonBlocking);
        if (sendCode < 0) {
            LogHelper::log(Warn, "Fail in socks forth handshake, couldn't send reply to localFd: %d", localFd);
            return sendCode;
        }
        LogHelper::log(Debug, "client socks handshake end");
    }
    else {
        LogHelper::log(Warn, "Unknown msgType: %d", msgType);
        return -2;
    }
    return 0;
}

int startListen(int serverFd, ClientContext *ctx) {
    int on = 1;
    int listen_sd = make_socket(constants::clientListenPort, on, constants::clientBindToLoopback);
    if (listen_sd < 0) {
        LogHelper::log(Error, "Client fail to make listen socket");
        return -1;
    }

    // set nonblocking
    int rc = ioctl(listen_sd, FIONBIO, (char *)&on);
    if (rc < 0) {
        // perror("ioctl() failed");
        LogHelper::log(Error, "failed to ioctl(), %s", strerror(errno));
        close(listen_sd);
        // exit(-1);
        return -2;
    }

    // rc = ioctl(serverFd, FIONBIO, (char *)&on);
    // if (rc < 0) {
    //     // perror("ioctl() failed");
    //     LogHelper::log(Error, "failed to ioctl(), %s", strerror(errno));
    //     close(serverFd);
    //     // exit(-1);
    //     return -2;
    // }

    rc = listen(listen_sd, 1024);
    if (rc < 0) {
        LogHelper::log(Error, "Failed to listen, %s", strerror(errno));
        close(listen_sd);
        return -3;
    }

    fd_set master_set, working_set;
    FD_ZERO(&master_set);
    int max_sd = listen_sd;
    FD_SET(listen_sd, &master_set);

    FD_SET(serverFd, &master_set);
    if (serverFd > max_sd) {
        max_sd = serverFd;
    }

    struct timeval timeout;
    timeout.tv_sec  = 3 * 60;
    timeout.tv_usec = 0;

    LocalFdMap localFdMap;
    LogHelper::log(Info, "Begin loop");
    bool endLoop = false;
    do {
        memcpy(&working_set, &master_set, sizeof(master_set));
        LogHelper::log(Debug, "Client waiting for select");
        rc = select(max_sd + 1, &working_set, NULL, NULL, &timeout);
        if (rc < 0) {
            LogHelper::log(Error, "Error when select, %s", strerror(errno));
            return -4;
        }
        if (rc == 0) {
            LogHelper::log(Debug, "select time out, continue");
            continue;
        }
        int ready_fds = rc;
        LogHelper::log(Debug, "read_fd num: %d", ready_fds);
        for (int i = 0; i <= max_sd && ready_fds > 0; ++i) {
            if (FD_ISSET(i, &working_set)) {
                ready_fds--;
                if (i == listen_sd) {
                    LogHelper::log(Debug, "Listen socket is readable");
                    int new_sd = -1;
                    do {
                        new_sd = accept(listen_sd, NULL, NULL);
                        if (new_sd < 0) {
                            if (errno != EAGAIN) {
                                // perror("  accept() failed");
                                LogHelper::log(Error, "accept failed, %s", strerror(errno));
                                endLoop = true;
                            }
                            break;
                        }

                        LogHelper::log(Debug, "New incoming connection fd: %d\n", new_sd);
                        FD_SET(new_sd, &master_set);
                        if (new_sd > max_sd)
                            max_sd = new_sd;
                        
                        rc = socksHandShake(new_sd, serverFd, ctx, localFdMap);
                        if (rc < 0) {
                            LogHelper::log(Error, "Failed to establish socks connection, clean fd: %d", new_sd);
                            close(new_sd);
                            FD_CLR(new_sd, &master_set);
                            if (new_sd == max_sd) {
                                while (FD_ISSET(max_sd, &master_set) == 0)
                                    max_sd -= 1;
                            }
                            if (localFdMap.find(new_sd) != localFdMap.end()) {
                                localFdMap.erase(new_sd);
                            }
                        }
                    } while (new_sd != -1);
                }
                else if(i == serverFd) {
                    LogHelper::log(Debug, "Server Fd is readable");

                    int handleRet = readAndHandleFromServer(i, localFdMap, ctx, master_set, max_sd);

                    
                }
                else {
                    LogHelper::log(Debug, "Fd %d is ready", i);
                    bool close_conn = false;
                    static char readBuf2[constants::AES_MAX_DATA_LENGTH - 72];
                    memset(readBuf2, 0, sizeof(readBuf2));
                    rc = recv(i, readBuf2, sizeof(readBuf2), 0);
                    if (rc < 0) {
                        if (errno != EAGAIN) {
                            LogHelper::log(Error, "Failed in recv of %d fd", i);
                            close_conn = true;
                        }
                        
                    }
                    else if (rc == 0) {
                        LogHelper::log(Warn, "Fd %d has been closed", i);
                        close_conn = true;
                    }
                    else {
                        LogHelper::log(Debug, "recv from socks user %d bytes, : %s", rc, readBuf2);
                        int sendCode = sendRequestMsg(serverFd, i, (const uint8_t *)readBuf2, rc, ctx);
                        if (sendCode < 0) {
                            LogHelper::log(Error, "Fail to send local data to server");
                            return sendCode;
                        }
                    }

                    if (close_conn) {
                        LogHelper::log(Warn, "Close fd: %d", i);
                        close(i);
                        FD_CLR(i, &master_set);
                        if (i == max_sd) {
                            while (FD_ISSET(max_sd, &master_set) == 0)
                                max_sd -= 1;
                        }
                        if (localFdMap.find(i) != localFdMap.end()) {
                            localFdMap.erase(i);
                        }
                    }
                }
            }
        }

    } while(!endLoop);

    return 0;
}

int launch_client() {

    // ignore signal
    signal(SIGPIPE, SIG_IGN);
    const char tempBuf[] = "Hello, test!";
    while (true) {
        ClientContext context;
        if (SocketPlugin::getInstance()->connectSocket(serverAddrStr.c_str(),  serverListenPort, constants::clientNonBlocking) < 0 ) {
            LogHelper::log(Error, "Client Fail to connect to server");
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            break;
        }
        ssize_t shakeCode = clientHandleShake(&context);
        if (shakeCode < 0) {
            return shakeCode;
        }
        
        LogHelper::log(Debug, "Hand shake success");
        int sockFd = SocketPlugin::getInstance()->getSockFd();
        // std::thread readThread(loopRead, sockFd, &context), writeThread(loopSend, sockFd, &context);
        // readThread.join();
        // writeThread.join();
        std::thread listenThread(startListen, sockFd, &context);
        listenThread.join();

        SocketPlugin::getInstance()->closeSocket();
        // break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return 0;
}