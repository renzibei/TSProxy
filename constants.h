#ifndef TS_CONSTANTS_H
#define TS_CONSTANTS_H

#include <string>
#include <stdint.h>

namespace constants {

    const std::string serverAddrStr = "127.0.0.1";
    const int serverListenPort = 20443;

    const std::string clientAddrStr = "127.0.0.1";
    const int clientListenPort = 11080;
    const int clientBindToLoopback = 0;

    enum MsgType {
        FirstHandShakeMsg = 1,
        SecondHandShakeMsg = 2,
        ThirdHandShakeMsg = 3,
        DebugC2SMsg = 4,
        DebugS2CMsg = 5,
        SocksTcpRequestMsg = 6,
        SocksTcpReplyMsg = 7,
        SocksTrafficRequestMsg = 8,
        SocksTrafficReplyMsg = 9
    };

    const int serverNonBlocking = 1;
    const int clientNonBlocking = 1;

    const uint8_t SocksVersion = 0x5;

    const int SocksMaxMethods = 0xff;
    const int SocksDomainMaxLength = 0xff;

    enum SocksRequestCmdType {
        SocksConnectCmd = 0x01,
        SocksBindCmd = 0x02,
        SocksUdpAssoCmd = 0x03
    };

    const uint8_t SocksNoAuthMethod = 0x00;
    const uint8_t SocksGSSAPIMethod = 0x01;
    const uint8_t SocksUnamePwMethod = 0x02;
    const uint8_t SocksNoSupportMethod = 0xff;

    const uint8_t SocksAddrIpv4Type = 0x01;
    const uint8_t SocksAddrDomainType = 0x03;
    const uint8_t SocksAddrIpv6Type = 0x04;

    const uint8_t TSPacketContentType = 0x17;
    const uint16_t TSPacketVersion = 0x0303;

    const size_t MAX_MSG_DATA_LENGTH = 65536 - 20;

    const size_t PACKET_BUFFER_SIZE = 65536;
    const size_t TS_PACKET_PAYLOAD_LENGTH = 65536;

    const size_t AES_MAX_DATA_LENGTH = MAX_MSG_DATA_LENGTH - 32;

    const time_t ClientTimeOutSeconds = 60 * 2;
    const time_t SocketTimeOutSeconds = 60 * 3;

    const char uuidKey[37] = "9D72C5C8-DC4B-47A6-890F-CBD6F128A82F";

    int getKey(char (&dst)[16]);

    
};

#endif