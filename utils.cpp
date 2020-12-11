#include "utils.h"

#include <unistd.h>
#include <cstring>
#include <cerrno>

#include "constants.h"
#include "aes.h"

#include <stdlib.h>
#include <time.h>


using namespace constants;

ssize_t readNBytes(int fd, void *buf, size_t nbyte) {
    size_t hasRead = 0;
    char* dest = (char*)buf;
    while(hasRead < nbyte) {
        ssize_t tempLen = read(fd, dest + hasRead, nbyte - hasRead);
        if(tempLen < 0) {
            LogHelper::log(Error, "Error happens when read fd , errno: %d %s", errno, strerror(errno));
            return -1;
        }
        hasRead += tempLen;
    }
    return 0;
}

ssize_t writeNBytes(int fd, const void *buf, size_t nbyte) {
    size_t hasWritten = 0;
    const char* src = (const char*)buf;
    while(hasWritten < nbyte) {
        ssize_t tempLen = write(fd, src + hasWritten, nbyte - hasWritten);
        if(tempLen < 0) {
            LogHelper::log(Error, "Errors when write fd, %s", strerror(errno));
            return -1;
        }
        hasWritten += tempLen;
    }
    return 0;
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

ssize_t sendTSPacket(int fd, const InnerMsg *msg) {
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
    return writeNBytes(fd, packetBuffer, dataLen);
}

ssize_t recvTSPacket(int fd, InnerMsg *msg) {
    static uint8_t packetBuffer[PACKET_BUFFER_SIZE];
    LogHelper::log(Debug, "sockFd: %d start to read 5bytes", fd);
    ssize_t readRet = readNBytes(fd, packetBuffer, 5);
    if (readRet != 0) {
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
    readRet = readNBytes(fd, packetBuffer + 5, msgLen);
    if (readRet != 0) {
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