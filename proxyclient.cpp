#include "proxyclient.h"
#include "SocketPlugin.h"
#include "constants.h"
#include "utils.h"

#include <chrono>
#include <thread>
#include <time.h>
#include <stdlib.h>
#include <string.h>


using namespace constants;

struct ClientContext {
    uint64_t cSeq, sSeq;
    ClientContext(uint64_t cSeq, uint64_t sSeq): cSeq(cSeq), sSeq(sSeq) {}
    ClientContext(): cSeq(0), sSeq(0) {}

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
    ssize_t sendCode = sendTSPacket(SocketPlugin::getInstance()->getSockFd(), &innerMsg);
    if (sendCode < 0) {
        LogHelper::log(Error, "client handle shake failed, fail to send first packet");
        return sendCode;
    }
    memset(&innerMsg, 0, sizeof(innerMsg));
    ssize_t recvCode = recvTSPacket(SocketPlugin::getInstance()->getSockFd(), &innerMsg);
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
    sendCode = sendTSPacket(SocketPlugin::getInstance()->getSockFd(), &innerMsg);
    if (sendCode < 0) {
        LogHelper::log(Error, "client handle shake failed, fail to send third packet");
        return sendCode;
    }

    *ctx = ClientContext(innerMsg.cSeq, innerMsg.sSeq);
    return 0;
}

ssize_t sendMessage(int sockFd, const uint8_t *src, size_t nbyte, ClientContext *ctx) {
    static InnerMsg innerMsg;
    memset(&innerMsg, 0, sizeof(innerMsg));
    ctx->cSeq++;
    innerMsg.cSeq = ctx->cSeq;
    innerMsg.sSeq = ctx->sSeq;
    innerMsg.msgType = 4;
    if (nbyte > sizeof(innerMsg.data)) {
        LogHelper::log(Error, "Failed to send data, nbyte too large, nbyte: %lu", nbyte);
        return -1;
    }
    innerMsg.dataLength = nbyte;
    memcpy(innerMsg.data, src, nbyte);
    ssize_t sendCode = sendTSPacket(sockFd, &innerMsg);
    if (sendCode < 0) {
        LogHelper::log(Error, "Fail to send data");
        return sendCode;
    }
    return 0;
}

ssize_t recvMessage(int sockFd, uint8_t *dst, size_t bufferSize, ClientContext *ctx) {
    static InnerMsg innerMsg;
    memset(&innerMsg, 0, sizeof(innerMsg));

    ssize_t recvCode = recvTSPacket(sockFd, &innerMsg);
    if (recvCode < 0) {
        LogHelper::log(Error, "Fail to recv data");
        return recvCode;
    }
    if (innerMsg.sSeq != ctx->sSeq + 1) {
        LogHelper::log(Error, "Fail to recv data, seq error, sSeq: %llu, expected: %llu", innerMsg.sSeq, ctx->sSeq + 1);
        return -2;
    }
    if (innerMsg.msgType != 5) {
        LogHelper::log(Error, "Fail to recv data, msgType error, recv type :%d, expected: 5", innerMsg.msgType);
        return -3;
    }
    if (innerMsg.dataLength > bufferSize) {
        LogHelper::log(Error, "Fail to recv data, bufferSize not enought, bufferSize :%lu, expected: %lu", bufferSize, innerMsg.dataLength);
        return -4;
    }
    memcpy(dst, innerMsg.data, innerMsg.dataLength);
    ctx->sSeq = innerMsg.sSeq;
    return 0;
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
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void loopRead(int sockFd, ClientContext *ctx) {
    static char buf[65536];
    while (true) {
        memset(buf, 0, sizeof(buf));
        ssize_t recvCode = recvMessage(sockFd, (uint8_t *)buf, sizeof(buf), ctx);
        if (recvCode < 0) {
            LogHelper::log(Error, "Fail to recv msg from server, break");
            break;
        }
        LogHelper::log(Debug, "recv msg from server: %s", buf);
        // std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

int launch_client() {
    const char tempBuf[] = "Hello, test!";
    while (true) {
        ClientContext context;
        if (SocketPlugin::getInstance()->connectSocket(serverAddrStr.c_str(),  serverListenPort) < 0 ) {
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
        std::thread readThread(loopRead, sockFd, &context), writeThread(loopSend, sockFd, &context);
        readThread.join();
        writeThread.join();

        SocketPlugin::getInstance()->closeSocket();
        // break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return 0;
}