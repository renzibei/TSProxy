#ifndef TS_CONSTANTS_H
#define TS_CONSTANTS_H

#include <string>

namespace constants {

    const std::string serverAddrStr = "127.0.0.1";
    const int serverListenPort = 10443;    

    const uint8_t TSPacketContentType = 0x17;
    const uint16_t TSPacketVersion = 0x0303;

    const size_t MAX_MSG_DATA_LENGTH = 65536 - 20;

    const size_t PACKET_BUFFER_SIZE = 65536;
    const size_t TS_PACKET_PAYLOAD_LENGTH = 65536;

    const size_t AES_MAX_DATA_LENGTH = MAX_MSG_DATA_LENGTH - 32;

    const time_t ClientTimeOutSeconds = 60 * 2;

    const char uuidKey[37] = "9D72C5C8-DC4B-47A6-890F-CBD6F128A82F";

    int getKey(char (&dst)[16]);

    
};

#endif