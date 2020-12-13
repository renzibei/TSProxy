#include "utils.h"

#include <unistd.h>
#include <cstring>
#include <cerrno>

#include "constants.h"
#include "aes.h"

#include <stdlib.h>
#include <time.h>
#include <thread>
#include <chrono>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/types.h>
#include <fcntl.h>




using namespace constants;

int tryConnectSocket(const char *addrStr, uint16_t port, int isNonBlocking) {
    struct addrinfo hints, *res, *res0;
    int error;
    int s;
    // const char *cause = NULL;
    std::string cause;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    // hints.ai_protocol = IPPROTO_TCP;
    char portStr[8] = {0};
    sprintf(portStr, "%u", port); 

    error = getaddrinfo(addrStr, portStr, &hints, &res0);
    if (error) {
            // errx(1, "%s", gai_strerror(error));
        LogHelper::log(Warn, "Fail to get addr info: %s", gai_strerror(error));
        return -1;
            /*NOTREACHED*/
    }
    s = -1;
    for (res = res0; res; res = res->ai_next) {
        s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (s < 0) {
            cause = "socket";
            continue;
        }

        if (isNonBlocking) {
            int status = fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
            if (status < 0) {
                LogHelper::log(Warn, "Fail to set non-blocking, %s", strerror(errno));
                cause = "cannot nonblocking";
                continue;
            }
        }
        

        if (connect(s, res->ai_addr, res->ai_addrlen) < 0) {
            if (errno != EINPROGRESS) {
                cause = "connect";
                close(s);
                s = -1;
                continue;
            }
            

        }

        static char addrS[constants::SocksDomainMaxLength];
        memset(addrS, 0, constants::SocksDomainMaxLength);
        const char* ss = inet_ntop(res->ai_family, &(((struct sockaddr_in *)(res->ai_addr))->sin_addr.s_addr), addrS, sizeof(addrS));
        if (ss == NULL) {
            LogHelper::log(Error, "Fail to convrt ntop");
        }
        else {
            LogHelper::log(Debug, "Family: %d solve %s ip as %s , port %s; INET family: %d", res->ai_family, addrStr, addrS, portStr, AF_INET);
        }

        break;  /* okay we got one */
    }
    if (s < 0) {
        LogHelper::log(Warn, "Tried all the ip, non connection could be established, cause: %s", cause.c_str());
        return -2;
            // err(1, "%s", cause);
            /*NOTREACHED*/
    }
    freeaddrinfo(res0);
    
    return s;
}

int make_socket(uint16_t port, int on, int bindToLoopback)
{
    int sock;
    struct sockaddr_in6 name;

    /* Create the socket. */    
    sock = socket(AF_INET6, SOCK_STREAM, 0);
    if (sock < 0)
    {
        LogHelper::log(Error, "failed to create socket, %s", strerror(errno));
        return -1;
    }

    // set reuseable
    int rc = setsockopt(sock, SOL_SOCKET,  SO_REUSEADDR,
                   (char *)&on, sizeof(on));
    if (rc < 0)
    {
        LogHelper::log(Error, "setsockopt() failed, %s", strerror(errno));
        close(sock);
        return -2;
    }

    /* Give the socket a name. */
    memset(&name, 0, sizeof(name));
    name.sin6_family = AF_INET6;
    name.sin6_port = htons (port);
    //   name.sin6_addr.s_addr = htonl (INADDR_ANY);
    
    if (bindToLoopback)
        memcpy(&name.sin6_addr, &in6addr_loopback, sizeof(in6addr_loopback));
    else
        memcpy(&name.sin6_addr, &in6addr_any, sizeof(in6addr_any));

    if (bind (sock, (struct sockaddr *) &name, sizeof (name)) < 0)
    {
        LogHelper::log(Error, "Failed to bind, %s", strerror(errno));
        return -3;
    }

    return sock;
}

ssize_t readNBytes(int fd, void *buf, size_t nbyte, int isNonBlocking) {
    size_t hasRead = 0;
    char* dest = (char*)buf;
    while(hasRead < nbyte) {
        ssize_t tempLen = recv(fd, dest + hasRead, nbyte - hasRead, 0);
        if(tempLen < 0) {
            if (isNonBlocking && errno == EAGAIN) {
                continue;
            }
            LogHelper::log(Error, "Error happens when read fd , errno: %d %s", errno, strerror(errno));
            return -1;
        }
        if(isNonBlocking && tempLen == 0) {
            LogHelper::log(Error, "Connection has been closed in sockFd %d when tried to recv", fd);
            return hasRead;
        }
        hasRead += tempLen;
        
    }
    
    return hasRead;
}

ssize_t writeNBytes(int fd, const void *buf, size_t nbyte, int isNonBlocking) {
    size_t hasWritten = 0;
    const char* src = (const char*)buf;
    

    while(hasWritten < nbyte) {
        ssize_t tempLen = send(fd, src + hasWritten, nbyte - hasWritten, 0);
        if(tempLen < 0) {
            if (isNonBlocking && errno == EAGAIN) {
                continue;
            }
            LogHelper::log(Error, "Errors when write fd, fd: %d, %s", fd, strerror(errno));
            return -1;
        }
        if (isNonBlocking && tempLen == 0) {
            LogHelper::log(Error, "Connection has been closed in sockFd %d when tried to send", fd);
            return hasWritten;
        }
        hasWritten += tempLen;
    }
    return hasWritten;
}

int disAssembleInnerMsg(InnerMsg *msg ,const uint8_t *src_data, size_t nbyte) {
    // memset(msg, 0, sizeof(InnerMsg));
    if (nbyte < sizeof(msg->cSeq) + sizeof(msg->sSeq) + sizeof(msg->msgType) + sizeof(msg->dataLength) ) {
        LogHelper::log(Warn, "nbyte less than InnerMsg header, nbyte: %lu", nbyte); 
        return -1;
    }
    ntoh_copy8bytes(&(msg->cSeq), src_data); src_data += sizeof(msg->cSeq);
    ntoh_copy8bytes(&(msg->sSeq), src_data); src_data += sizeof(msg->sSeq);
    ntoh_copy2bytes(&(msg->msgType), src_data); src_data += sizeof(msg->msgType);
    ntoh_copy2bytes(&(msg->dataLength), src_data); src_data += sizeof(msg->dataLength);
    memcpy(msg->data, src_data, msg->dataLength);
    return 0;
}

int disAssembleTSPacket(TSPacket *packet, const uint8_t *src_data, size_t nbyte) {
    if (nbyte < sizeof(packet->contentType)) {
        LogHelper::log(Warn, "nbyte less than sizeof contentType, byte: %lu", nbyte);
        return -1;
    }
    packet->contentType = *src_data++; nbyte -= sizeof(packet->contentType);
    if(nbyte < sizeof(packet->tsVersion)) {
        LogHelper::log(Warn, "nbyte less than sizeof tsVersion, byte: %lu", nbyte);
        return -2;
    }
    ntoh_copy2bytes(&(packet->tsVersion), src_data); src_data += sizeof(packet->tsVersion); nbyte -= sizeof(packet->tsVersion);
    if(nbyte < sizeof(packet->length)) {
        LogHelper::log(Warn, "nbyte less than sizeof length, byte: %lu", nbyte);
        return -3;
    }
    ntoh_copy2bytes(&(packet->length), src_data); src_data += sizeof(packet->length); nbyte -= sizeof(packet->length);
    if(nbyte < packet->length) {
        LogHelper::log(Warn, "nbyte less than packet->length, byte: %lu", nbyte);
        return -4;
    }

    size_t realMsgLen = packet->length - sizeof(packet->random) - sizeof(packet->mac);

    memcpy(packet->msgData, src_data, realMsgLen); src_data += realMsgLen; nbyte -= realMsgLen;
    if(nbyte < sizeof(packet->random) + sizeof(packet->mac)) {
        LogHelper::log(Warn, "nbyte less than sizeof random + mac, byte: %lu", nbyte);
        return -5;
    } 
    // const uint8_t *tempSrc = src_data;
    memcpy(packet->random, src_data, sizeof(packet->random)); src_data += sizeof(packet->random); nbyte -= sizeof(packet->random);
    memcpy(packet->mac, src_data, sizeof(packet->mac));

    // fprintf(stderr, "when disassemble random: ");
    // for(int i = 0; i < 16; ++i) {
    //     fprintf(stderr, "%u ", (uint8_t)tempSrc[i]);
    // }
    // fprintf(stderr, "\nmac: ");
    // for(int i = 0; i < 16; ++i) {
    //     fprintf(stderr, "%u ", (uint8_t)tempSrc[16 + i]);
    // }
    // fprintf(stderr, "\n");
    return 0;
    // disAssembleInnerMsg(&(packet->msgData), src_data, nbyte);
    
}

ssize_t assembleInnerMsg(uint8_t *dst, size_t buffer_size, const InnerMsg *src_msg) {
    uint8_t *old_dst = dst;
    if (buffer_size < sizeof(src_msg->cSeq) + sizeof(src_msg->sSeq) + sizeof(src_msg->msgType) + sizeof(src_msg->dataLength)) {
        LogHelper::log(Warn, "buffer size less than inner msg header, buffer_size: %lu");
        return -1;
    }
    hton_copy8bytes(dst, &(src_msg->cSeq)); dst += sizeof(src_msg->cSeq);
    hton_copy8bytes(dst, &(src_msg->sSeq)); dst += sizeof(src_msg->sSeq);
    hton_copy2bytes(dst, &(src_msg->msgType)); dst += sizeof(src_msg->msgType);
    hton_copy2bytes(dst, &(src_msg->dataLength)); dst += sizeof(src_msg->dataLength);
    buffer_size -= sizeof(src_msg->cSeq) + sizeof(src_msg->sSeq) + sizeof(src_msg->dataLength);
    if (buffer_size < src_msg->dataLength) {
        LogHelper::log(Warn, "buffer size less than InnerMsg dataLength, buffer_size: %lu, dataLength: %lu", buffer_size, src_msg->dataLength);
        return -2;
    }
    memcpy(dst, src_msg->data, src_msg->dataLength); dst += src_msg->dataLength;
    return dst - old_dst; 
}

ssize_t assembleTSPacket(uint8_t *dst, size_t buffer_size, const TSPacket *src_packet) {
    uint8_t *old_dst = dst;
    if (buffer_size < sizeof(src_packet->contentType) + sizeof(src_packet->tsVersion) + sizeof(src_packet->length) ) {
        LogHelper::log(Warn, "buffer size less than TSPacket header, buffer_size: %lu", buffer_size);
        return -1;
    }
    *dst++ = src_packet->contentType; 
    hton_copy2bytes(dst, &(src_packet->tsVersion)); dst += sizeof(src_packet->tsVersion);
    hton_copy2bytes(dst, &(src_packet->length)); dst += sizeof(src_packet->length);
    buffer_size -= sizeof(src_packet->contentType) + sizeof(src_packet->tsVersion) + sizeof(src_packet->length);

    size_t realMsgLen = src_packet->length - sizeof(src_packet->random) - sizeof(src_packet->mac);
    if (buffer_size < realMsgLen) {
        LogHelper::log(Warn, "buffer size less than TSPacket payload length, buffer_size: %lu, payload length: %lu", buffer_size, realMsgLen);
        return -2;
    }
    // size_t delta_dst = assembleInnerMsg(dst, &(src_packet->msgData)); dst += delta_dst;
    
    memcpy(dst, src_packet->msgData, realMsgLen); dst += realMsgLen;
    buffer_size -= realMsgLen;
    if (buffer_size < sizeof(src_packet->random) + sizeof(src_packet->mac)) {
        LogHelper::log(Warn, "buffer size less than TSPacket random, mac");
        return -3;
    }
    // uint8_t *tempDst = dst;
    memcpy(dst, src_packet->random, sizeof(src_packet->random));
    dst += sizeof(src_packet->random);
    memcpy(dst, src_packet->mac, sizeof(src_packet->mac));
    dst += sizeof(src_packet->mac);

    // fprintf(stderr, "when assemble random: ");
    // for(int i = 0; i < 16; ++i) {
    //     fprintf(stderr, "%u ", (uint8_t)tempDst[i]);
    // }
    // fprintf(stderr, "\nmac: ");
    // for(int i = 0; i < 16; ++i) {
    //     fprintf(stderr, "%u ", (uint8_t)tempDst[16 + i]);
    // }
    // fprintf(stderr, "\n");

    return dst - old_dst;
}

ssize_t sendTSPacket(int fd, const InnerMsg *msg, int isNonBlocking) {
    static TSPacket packetStruct;
    // static InnerMsg msgStruct;
    memset(&packetStruct, 0, sizeof(TSPacket));
    // memset(&msgStruct, 0, sizeof(msgStruct));
    
    
    // msgStruct.cSeq = cSeq;
    // msgStruct.sSeq = sSeq;
    // msgStruct.dataLength = nbyte;
    // memcpy(msgStruct.data, src, nbyte);
    static uint8_t packetBuffer[PACKET_BUFFER_SIZE];
    memset(packetBuffer, 0, PACKET_BUFFER_SIZE);

    ssize_t dataLen = assembleInnerMsg(packetBuffer, sizeof(packetBuffer), msg);
    if (dataLen < 0) {
        LogHelper::log(Warn, "Fail to assemble InnerMsg, retCode: %d", dataLen);
        return dataLen;
    }
    char key[16], random[16], mac[16];
    int keyRet = constants::getKey(key);
    if (keyRet != 0) {
        LogHelper::log(Warn, "Fail to get key");
        return keyRet;
    } 
    int chiperLen = encrypt((char *)(packetStruct.msgData), sizeof(packetStruct.msgData), (const char*)packetBuffer, dataLen, key, random, mac);
    if (chiperLen < 0) {
        LogHelper::log(Warn, "Fail to encrypt when send TSPacket");
        return chiperLen;
    }
    
    packetStruct.contentType = TSPacketContentType;
    packetStruct.tsVersion = TSPacketVersion;
    packetStruct.length = chiperLen + sizeof(random) + sizeof(mac);
    memcpy(packetStruct.random, random, sizeof(random));
    memcpy(packetStruct.mac, mac, sizeof(mac));

    // fprintf(stderr, "when send, key: ");
    // for(int i = 0; i < 16; ++i) {
    //     fprintf(stderr, "%u ", (uint8_t)key[i]);
    // }
    // fprintf(stderr, "\nrandom: ");
    // for(int i = 0; i < 16; ++i) {
    //     fprintf(stderr, "%u ", (uint8_t)packetStruct.random[i]);
    // }
    // fprintf(stderr, "\nmac: ");
    // for(int i = 0; i < 16; ++i) {
    //     fprintf(stderr, "%u ", (uint8_t)packetStruct.mac[i]);
    // }
    // fprintf(stderr, "\n");

    dataLen = assembleTSPacket(packetBuffer, sizeof(packetBuffer), &packetStruct);
    if (dataLen < 0) {
        LogHelper::log(Warn, "Fail to assemble TSPacket, retCode: %d", dataLen);
        return dataLen;
    }
    // size_t dataLen = assembleTSPacket(packetBuffer, &packetStruct);
    LogHelper::log(Debug, "sockFd: %d start to write %d bytes", fd, dataLen);
    return writeNBytes(fd, packetBuffer, dataLen, isNonBlocking);
}

ssize_t recvTSPacket(int fd, InnerMsg *msg, int isNonBlocking) {
    static uint8_t packetBuffer[PACKET_BUFFER_SIZE];
    LogHelper::log(Debug, "sockFd: %d start to read 5bytes", fd);
    ssize_t readRet = readNBytes(fd, packetBuffer, 5, isNonBlocking);
    if (readRet < 0) {
        return readRet;
    }
    uint8_t tsContentType = *packetBuffer;
    if (tsContentType != TSPacketContentType) {
        LogHelper::log(Warn, "content type not right");
        return -1;
    }
    uint16_t msgLen = 0, tsVersion = 0;
    ntoh_copy2bytes(&tsVersion, packetBuffer + 1);
    
    if (tsVersion != TSPacketVersion) {
        LogHelper::log(Warn, "Packet version not equals to tsversion");
        return -2;
    }

    ntoh_copy2bytes(&(msgLen), packetBuffer + 3);
    LogHelper::log(Debug, "sockFd: %d start to read %d bytes", fd, msgLen);
    readRet = readNBytes(fd, packetBuffer + 5, msgLen, isNonBlocking);
    if (readRet < 0) {
        return readRet;
    }
    static TSPacket packetStruct;
    memset(&packetStruct, 0, sizeof(packetStruct));
    int code = disAssembleTSPacket(&packetStruct, packetBuffer, msgLen + 5);
    if (code < 0) {
        LogHelper::log(Warn, "Fail to disassemble TSPacket in recv");
        return code;
    }
    char key[16];
    int keyRet = constants::getKey(key);
    if(keyRet != 0) {
        LogHelper::log(Error, "Fail to get key");
        return -3;
    }

    // fprintf(stderr, "when recv key: ");
    // for(int i = 0; i < 16; ++i) {
    //     fprintf(stderr, "%d ", (uint8_t)key[i]);
    // }
    // fprintf(stderr, "\nrandom: ");
    // for(int i = 0; i < 16; ++i) {
    //     fprintf(stderr, "%u ", (uint8_t)packetStruct.random[i]);
    // }
    // fprintf(stderr, "\nmac: ");
    // for(int i = 0; i < 16; ++i) {
    //     fprintf(stderr, "%u ", (uint8_t)packetStruct.mac[i]);
    // }
    // fprintf(stderr, "\n");

    int textLen = decrypt((char*)packetBuffer, sizeof(packetBuffer), (const char*)packetStruct.msgData, packetStruct.length - 32, key, packetStruct.random, packetStruct.mac);
    if (textLen < 0) {
        LogHelper::log(Warn, "Fail to decrypt when recv ts packet");
        return -4;
    }


    code = disAssembleInnerMsg(msg, packetBuffer, textLen);
    if (code < 0) {
        LogHelper::log(Warn, "Fail to disassemble inner msg in recv");
        return code;
    }
    return 0;
}



uint64_t getRand63() {
    uint64_t ret = 0;
    for (int i = 0; i < 63; ++i) {
        ret = (ret << 1) + (rand() & 1);
    }
    ret &= 0x7fffffffffffffffULL;
    return ret;
}