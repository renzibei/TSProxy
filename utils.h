#ifndef TS_UTILS_H
#define TS_UTILS_H

#include <sys/types.h>
#include "loghelper.h"
#include <stdint.h>
#include <arpa/inet.h>
#include "constants.h"


ssize_t readNBytes(int fd, void *buf, size_t nbyte);
ssize_t writeNBytes(int fd, const void *buf, size_t nbyte);



/**
 * 
 * msgType: 1 for first handshake msg, 2 for second msg, 3 for third msg, 4 for packet from client to server, 5 for packet from server to client
 * */

struct InnerMsg {
    uint64_t cSeq, sSeq;
    uint16_t msgType;
    uint16_t dataLength;
    uint8_t data[constants::MAX_MSG_DATA_LENGTH];
};






struct TSPacket{
    uint8_t contentType;
    uint16_t tsVersion;
    uint16_t length;
    
    
    // InnerMsg msgData;
    uint8_t msgData[constants::TS_PACKET_PAYLOAD_LENGTH];
    char random[16], mac[16];
    
};

#if defined(__linux__)
#  include <endian.h>

#elif defined(__APPLE__)

#include <libkern/OSByteOrder.h>
#define htobe16(x) OSSwapHostToBigInt16(x)
#define htole16(x) OSSwapHostToLittleInt16(x)
#define be16toh(x) OSSwapBigToHostInt16(x)
#define le16toh(x) OSSwapLittleToHostInt16(x)
#define htobe32(x) OSSwapHostToBigInt32(x)
#define htole32(x) OSSwapHostToLittleInt32(x)
#define be32toh(x) OSSwapBigToHostInt32(x)
#define le32toh(x) OSSwapLittleToHostInt32(x)
#define htobe64(x) OSSwapHostToBigInt64(x)
#define htole64(x) OSSwapHostToLittleInt64(x)
#define be64toh(x) OSSwapBigToHostInt64(x)
#define le64toh(x) OSSwapLittleToHostInt64(x)



#elif defined(__FreeBSD__) || defined(__NetBSD__)
#  include <sys/endian.h>
#elif defined(__OpenBSD__)
#  include <sys/types.h>
#  define be16toh(x) betoh16(x)
#  define be32toh(x) betoh32(x)
#  define be64toh(x) betoh64(x)
#endif

#define copy1byte(dst, src) ( *((uint8_t *)(dst)) = *((uint8_t *)(src)) )
#define copy2bytes(dst, src) ( *((uint16_t *)(dst)) = *((uint16_t *)(src)) )
#define copy4bytes(dst, src) ( *((uint32_t *)(dst)) = *((uint32_t *)(src)) )
#define copy8bytes(dst, src) ( *((uint64_t *)(dst)) = *((uint64_t *)(src)) )

#define ntoh_copy2bytes(dst, src) ( *((uint16_t *)(dst)) = ntohs(*((uint16_t *)(src))) )
#define ntoh_copy4bytes(dst, src) ( *((uint32_t *)(dst)) = ntohl(*((uint32_t *)(src))) )
#define ntoh_copy8bytes(dst, src) ( *((uint64_t *)(dst)) = be64toh(*((uint64_t *)(src))) )

#define hton_copy2bytes(dst, src) ( *((uint16_t *)(dst)) = htons(*((uint16_t *)(src))) )
#define hton_copy4bytes(dst, src) ( *((uint32_t *)(dst)) = htonl(*((uint32_t *)(src))) )
#define hton_copy8bytes(dst, src) ( *((uint64_t *)(dst)) = htobe64(*((uint64_t *)(src))) )

ssize_t sendTSPacket(int fd, const InnerMsg *msg);
ssize_t recvTSPacket(int fd, InnerMsg *msg) ;

uint64_t getRand63();


#endif