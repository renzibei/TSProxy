#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include "proxyserver.h"
#include "constants.h"
#include "utils.h"

#include <unordered_map>
#include <deque>

#define SERVER_PORT  constants::serverListenPort

#define TRUE             1
#define FALSE            0

struct SocketNode {
    int sockFd;
    time_t lastTime;
    SocketNode(): sockFd(0), lastTime(0) {}
    SocketNode(int sockFd, time_t lastTime): sockFd(sockFd), lastTime(lastTime) {}
};

typedef std::unordered_map<int, SocketNode> SocketMap;

struct ClientNode{
    uint64_t cSeq, sSeq;
    time_t lastTime;
    SocketMap sMap;
    ClientNode(uint64_t cSeq, uint64_t sSeq, time_t lastTime): cSeq(cSeq), sSeq(sSeq), lastTime(lastTime) {}
    ClientNode():cSeq(0), sSeq(0), lastTime(0) {}
};

enum HostNodeStatus {
    HostWaiting = 0, HostReady = 1
};

struct HostNode {
    int clientFd, localFd;
    time_t lastTime;
    // HostNodeStatus status;
    std::deque<uint8_t> writeBuf;
    
    HostNode(int clientFd, int localFd, time_t lastTime): clientFd(clientFd), localFd(localFd), lastTime(lastTime) {}
    HostNode(): clientFd(0), localFd(0), lastTime(0){}
    
    void writeToBuffer(const uint8_t *src, size_t nbyte) {
        for(size_t i = 0; i < nbyte; ++i) {
            writeBuf.push_back(src[i]);
        }
    }

    ssize_t readFromBuffer(uint8_t *dst, size_t nbyte) {
        size_t limit = std::min(writeBuf.size(), nbyte);
        for(size_t i = 0; i < limit; ++i) {
            dst[i] = writeBuf[i];
        }
        return limit;
    }

    void popBuffer(size_t nbyte) {
        for(size_t i = 0; i < nbyte; ++i) {
            writeBuf.pop_front();
        }
    }
};

typedef std::unordered_map<int, HostNode> HostMap; 
typedef std::unordered_map<int, ClientNode*> ClientMap;


#define NON_BLOCKING



ssize_t serverHandShake(int sockFd, ClientMap &cMap) {
    uint64_t sSeq = getRand63();
    static InnerMsg innerMsg;
    memset(&innerMsg, 0, sizeof(innerMsg));

    ssize_t recvCode = recvTSPacket(sockFd, &innerMsg, constants::serverNonBlocking);
    if (recvCode < 0) {
        LogHelper::log(Error, "server hand shake failed, fail to recv first packet");
        return recvCode;
    }
    if (innerMsg.msgType != 1) {
        LogHelper::log(Error, "server hand shake failed, first packet type get: %d", innerMsg.msgType);
        return -4;
    }

    uint64_t cSeq = innerMsg.cSeq;

    innerMsg.sSeq = sSeq;
    innerMsg.msgType = 2;
    innerMsg.dataLength = 0;
    ssize_t sendCode = sendTSPacket(sockFd, &innerMsg, constants::serverNonBlocking);
    if (sendCode < 0) {
        LogHelper::log(Error, "server hand shake shake failed, fail to send second packet");
        return sendCode;
    }

    memset(&innerMsg, 0, sizeof(innerMsg));
    recvCode = recvTSPacket(sockFd, &innerMsg, constants::serverNonBlocking);
    if (recvCode < 0) {
        LogHelper::log(Error, "server hand shake failed, fail to recv third packet");
    }
    if (innerMsg.sSeq != sSeq || innerMsg.cSeq != cSeq + 1) {
        LogHelper::log(Error, "server hand shake failed, third packet receive seq not inc, receive cSeq: %llu, expected: %llu, receive sSeq: %llu, expected: %llu", innerMsg.cSeq, cSeq + 1, innerMsg.sSeq, sSeq);
        return -3;
    }
    if (innerMsg.msgType != 3) {
        LogHelper::log(Error, "server hand shake failed, third packet type get: %d", innerMsg.msgType);
        return -5;
    }
    
    cMap[sockFd] = new ClientNode(innerMsg.cSeq, innerMsg.sSeq, time(NULL));
    LogHelper::log(Debug, "Server handshake completed");
    
    return 0;
}

int sendData(int sockFd, const uint8_t *data, size_t nbyte, ClientMap &cMap, int msgType = 5) {
    ClientNode *node = cMap[sockFd];
    static InnerMsg innerMsg;
    memset(&innerMsg, 0, sizeof(innerMsg));
    node->sSeq++;
    
    innerMsg.cSeq = node->cSeq;
    innerMsg.sSeq = node->sSeq;
    innerMsg.msgType = msgType;
    if (nbyte > sizeof(innerMsg.data)) {
        LogHelper::log(Error, "Failed to send data, nbyte too large, nbyte: %lu", nbyte);
        return -1;
    }
    innerMsg.dataLength = nbyte;
    memcpy(innerMsg.data, data, nbyte);
    ssize_t sendCode = sendTSPacket(sockFd, &innerMsg, constants::serverNonBlocking);
    if (sendCode < 0) {
        LogHelper::log(Error, "Fail to send data");
        return sendCode;
    }
    node->lastTime = time(NULL);
    return 0;
}

int recvData(int sockFd, uint8_t *dst, size_t bufferSize, ClientMap &cMap, uint8_t *msgTypePtr = NULL) {
    static InnerMsg innerMsg;
    memset(&innerMsg, 0, sizeof(innerMsg));
    ClientNode *node = cMap[sockFd];
    ssize_t recvCode = recvTSPacket(sockFd, &innerMsg, constants::serverNonBlocking);
    if (recvCode < 0) {
        LogHelper::log(Error, "Fail to recv data");
        return recvCode;
    }
    
    if (innerMsg.cSeq != node->cSeq + 1 ) {
        LogHelper::log(Error, "Fail to recv data, seq error, cSeq: %llu, expected: %llu", innerMsg.cSeq, node->cSeq + 1);
        return -2;
    }
    if (innerMsg.msgType != 4 && innerMsg.msgType != 6 && innerMsg.msgType != 8) {
        LogHelper::log(Error, "Fail to recv data, msgType error, recv type :%d, expected: 4", innerMsg.msgType);
        return -3;
    }
    if (msgTypePtr != NULL) {
        *msgTypePtr = innerMsg.msgType;
    }
    if (innerMsg.dataLength > bufferSize) {
        LogHelper::log(Error, "Fail to recv data, bufferSize not enought, bufferSize :%lu, expected: %lu", bufferSize, innerMsg.dataLength);
        return -4;
    }
    memcpy(dst, innerMsg.data, innerMsg.dataLength);
    node->cSeq = innerMsg.cSeq;
    node->lastTime = time(NULL);
    return innerMsg.dataLength;
}



int recvAndHandleClientData(int sockFd, ClientMap &cMap, HostMap &h2lMap, fd_set &readFdSet, fd_set &writeFdSet, int &max_read_sd, int &max_write_sd, int &max_all_sd) {
    static uint8_t buf[constants::MAX_MSG_DATA_LENGTH];
    memset(buf, 0, sizeof(buf));
    uint8_t msgType = 0;
    int recvLen = recvData(sockFd, buf, sizeof(buf), cMap, &msgType);
    if (recvLen < 0) {
        LogHelper::log(Error, "Failed to recv data from client: %d", sockFd);
        return recvLen;
    }
    LogHelper::log(Debug, "Has recv a msg from client: %d, goning to handleit", sockFd);
    using namespace constants;
    SocketMap *l2hMap = &(cMap[sockFd]->sMap);
    if (msgType == constants::SocksTcpRequestMsg) {
        LogHelper::log(Debug, "it's a tcp request msg");
        static char dstAddrBuf[SocksDomainMaxLength];
        memset(dstAddrBuf, 0, sizeof(dstAddrBuf));
        uint8_t respCode = 0x00;
        uint8_t requestCmd = buf[0], dstAddrType = buf[1];
        int localFd = -1;
        if (requestCmd == SocksConnectCmd) {
            uint16_t dstPort = 0;
            if (dstAddrType == SocksAddrIpv4Type) {
                if (recvLen < 2 + 4 + 2 + 4) {
                    LogHelper::log(Warn, "len less than ipv4 addr, recvLen: %d", recvLen);
                    respCode = 0xff;
                }
                else {
                    if (inet_ntop(AF_INET, buf + 2, dstAddrBuf, sizeof(dstAddrBuf)) == NULL) {
                        LogHelper::log(Warn, "Fail to convert ipv4 num to str, %s", strerror(errno));
                        respCode = 0xff;
                    }
                    else {
                        ntoh_copy2bytes(&dstPort, buf + 6);
                        ntoh_copy4bytes(&localFd, buf + 8);
                    }
                }
            }
            else if (dstAddrType == SocksAddrIpv6Type) {
                if (recvLen < 2 + 16 + 2 + 4) {
                    LogHelper::log(Warn, "len less than ipv6 addr, recvLen: %d", recvLen);
                    respCode = 0xff;
                }
                else {
                    if (inet_ntop(AF_INET6, buf + 2, dstAddrBuf, sizeof(dstAddrBuf)) == NULL) {
                        LogHelper::log(Warn, "Fail to convert ipv6 num to str, %s", strerror(errno));
                        respCode = 0xff;
                    }
                    else {
                        ntoh_copy2bytes(&dstPort, buf + 18);
                        ntoh_copy4bytes(&localFd, buf + 20);
                    }
                }
            }
            else if (dstAddrType == SocksAddrDomainType) {
                if (recvLen < 2 + 1) {
                    LogHelper::log(Warn, "len less than domain header, recvLen: %d", recvLen);
                    respCode = 0xff;
                }
                else {
                    uint8_t domainAddrLen = buf[2];
                    if (recvLen < 2 + 1 + domainAddrLen + 2 + 4) {
                        LogHelper::log(Warn, "len less than domain addr, recvLen: %d", recvLen);
                        respCode = 0xff;
                    }
                    else {
                        memcpy(dstAddrBuf, buf + 3, domainAddrLen);
                        ntoh_copy2bytes(&dstPort, buf + 3 + domainAddrLen);
                        ntoh_copy4bytes(&localFd, buf + 3 + domainAddrLen + 2);
                    }
                }
            }
            else {
                LogHelper::log(Warn, "Unknown addrtype, type: %d", dstAddrType);
                respCode = 0xff;
            }

            if (respCode == 0x00) {
                
                LogHelper::log(Debug, "begin to try to connect socket to host");
                
                int hostSd = tryConnectSocket(dstAddrBuf, dstPort, constants::serverNonBlocking);
                LogHelper::log(Debug, "try end");
                if (hostSd < 0) {
                    respCode = 0xff;
                }
                else {
                    time_t nowT = time(NULL);
                    (*l2hMap)[localFd] = SocketNode(hostSd, nowT);
                    h2lMap[hostSd] = HostNode(sockFd, localFd, nowT);
                    FD_SET(hostSd, &readFdSet);
                    if (hostSd > max_read_sd) {
                        max_read_sd = hostSd;
                    }
                    FD_SET(hostSd, &writeFdSet);
                    if (hostSd > max_write_sd) {
                        max_write_sd = hostSd;
                    }
                    max_all_sd = std::max(max_read_sd, max_write_sd);
                    LogHelper::log(Debug, "Add hostFd :%d", hostSd);

                }
            }


        }
        else {
            respCode = 0xff;
        }

        

        static uint8_t replyMsgBuf[MAX_MSG_DATA_LENGTH];
        memset(replyMsgBuf, 0, sizeof(replyMsgBuf));
        replyMsgBuf[0] = respCode;
        replyMsgBuf[1] = SocksAddrIpv4Type;
        memset(replyMsgBuf + 2, 0, 4);
        uint16_t fakePort = 1728;
        hton_copy2bytes(replyMsgBuf + 6 , &fakePort);
        hton_copy4bytes(replyMsgBuf + 6 + sizeof(fakePort), &localFd);
        size_t replyLen = 1 + 1 + 4 + sizeof(fakePort) + sizeof(localFd);
        int sendCode = sendData(sockFd, replyMsgBuf, replyLen, cMap, SocksTcpReplyMsg);
        if (sendCode < 0) {
            LogHelper::log(Error, "Failed to send socks forth handshake reply from server");
            return sendCode;
        }
        
        
    }
    else if (msgType == SocksTrafficRequestMsg) {
        LogHelper::log(Debug, "it's a traffic request msg");

        int localFd = -1;
        if (recvLen < sizeof(localFd)) {
            LogHelper::log(Error, "Fail to receive socks traffic request, length less than localFd");
            return -5;
        }
        ntoh_copy4bytes(&localFd, buf);
        auto l2hIt = l2hMap->find(localFd);
        if (l2hIt == l2hMap->end()) {
            LogHelper::log(Warn, "Can not find localFd %d in l2hMap", localFd);
            return 0;
        }
        int hostSd = (*l2hMap)[localFd].sockFd;
        h2lMap[hostSd].writeToBuffer(buf + sizeof(localFd), recvLen - sizeof(localFd));
        // ssize_t sendCode = writeNBytes(hostSd, buf + sizeof(localFd), recvLen - sizeof(localFd), constants::serverNonBlocking);
        // if (sendCode < 0) {
        //     LogHelper::log(Warn, "Failed to send data to host, localFd: %d, will close hostSd", localFd);
        //     close(hostSd);
        //     FD_CLR(hostSd, &readFdSet);
        //     if (hostSd == max_read_sd) {
        //         while (FD_ISSET(max_read_sd, &readFdSet) == 0)
        //             max_read_sd -= 1;
        //     }
        //     l2hMap->erase(localFd);
        //     h2lMap.erase(hostSd);
        //     return -7;
        // }
        LogHelper::log(Debug, "End Send %d bytes to host: %d",recvLen - sizeof(localFd), hostSd);
        // for (int k = 0; k < recvLen - sizeof(localFd); ++k) {
        //     fprintf(stderr, "%c", buf[sizeof(localFd) + k]);
        // }
        // fprintf(stderr, "\n");
        time_t nowT = time(NULL);
        l2hIt->second.lastTime = nowT;
        // h2lMap[hostSd].lastTime = nowT;
    }
    else if (msgType == DebugC2SMsg) {
        LogHelper::log(Debug, "Received Debug C2S msg: %s", buf);
    }
    else {
        LogHelper::log(Warn, "Received Unknown msg type from client: %d", msgType);
        return -8;
    }

    return 0;
}

void eraseHostFromFdSets(int hostFd, fd_set &readSet, fd_set &writeSet, int &max_read_sd, int &max_write_sd, int &max_all_sd) {
    
    if (FD_ISSET(hostFd, &writeSet)) {
        close(hostFd);
        FD_CLR(hostFd, &writeSet);
        if (hostFd == max_write_sd) {
            while (FD_ISSET(max_write_sd, &writeSet) == 0 && max_write_sd > 0)
                max_write_sd -= 1;
        }
    }
    else {
        LogHelper::log(Warn, "hostFd :%d not in writeSet");
    }
    if (FD_ISSET(hostFd, &readSet)) {
        FD_CLR(hostFd, &readSet);
        if (hostFd == max_read_sd) {
            while (FD_ISSET(max_read_sd, &readSet) == 0)
                max_read_sd -= 1;
        }
    }
    else {
        LogHelper::log(Warn, "hostFd: %d not in readSet");
    }
    
    max_all_sd = std::max(max_read_sd, max_write_sd);
}

void eraseClientFromFdSet(int clientFd, fd_set &readSet, int &max_read_sd, int max_write_sd, int &max_all_sd) {
    close(clientFd);
    FD_CLR(clientFd, &readSet);
    if (clientFd == max_read_sd) {
        while (FD_ISSET(max_read_sd, &readSet) == 0)
            max_read_sd -= 1;
    }
    max_all_sd = std::max(max_read_sd, max_write_sd);
}

void cleanDeadLocalFds(int clientFd, HostMap &h2lMap, SocketMap &sMap, fd_set &readSet, fd_set &writeSet, int &max_read_sd, int &max_write_sd, int &max_all_sd, bool forceClean = false) {
    time_t now = time(NULL);
    for (auto it = sMap.begin(); it != sMap.end();) {
        if (forceClean || now - it->second.lastTime > constants::SocketTimeOutSeconds) {
            int localFd = it->first, hostFd = it->second.sockFd;
            LogHelper::log(Debug, "Clean timeout localFd %d in clientFd: %d, forceClean: %d", localFd, clientFd, forceClean);
            eraseHostFromFdSets(hostFd, readSet, writeSet, max_read_sd, max_write_sd, max_all_sd);
            h2lMap.erase(hostFd);
            it = sMap.erase(it);
            
        }
        else it++;
    }
}

void cleanDeadHosts(ClientMap &cMap, HostMap &sMap, fd_set &readSet, fd_set &writeSet, int &max_read_sd, int &max_write_sd, int &max_all_sd) {
    time_t now = time(NULL);
    for (auto it = sMap.begin(); it != sMap.end();) {
        if (now - it->second.lastTime > constants::SocketTimeOutSeconds) {
            LogHelper::log(Debug, "Clean timeout host fd: %d, localFd: %d", it->first, it->second.localFd);
            eraseHostFromFdSets(it->first, readSet, writeSet, max_read_sd, max_write_sd, max_all_sd);
            cMap[it->second.clientFd]->sMap.erase(it->second.localFd);
            it = sMap.erase(it);
        }
        else it++;
    }
}

void cleanDeadClients(ClientMap &cMap, HostMap &h2lMap, fd_set &readSet, fd_set &writeSet, int &max_read_sd, int &max_write_sd, int &max_all_sd) {
    time_t now = time(NULL);
    for (auto it = cMap.begin(); it != cMap.end(); ) {
        if (now - it->second->lastTime > constants::ClientTimeOutSeconds) {
            LogHelper::log(Debug, "Clean timeout client fd: %d", it->first);
            cleanDeadLocalFds(it->first, h2lMap, it->second->sMap, readSet, writeSet, max_read_sd, max_write_sd, max_all_sd, true);
            eraseClientFromFdSet(it->first, readSet, max_read_sd, max_write_sd, max_all_sd);
            delete it->second;
            it = cMap.erase(it);
        }
        else {
            cleanDeadLocalFds(it->first, h2lMap, it->second->sMap, readSet, writeSet, max_read_sd, max_write_sd, max_all_sd);
            it++;
        }
    }
}



int launch_server()
{
   int    i, len, rc, on = 1;
   int    listen_sd, max_read_sd, new_sd, max_write_sd, max_all_sd;
   int    desc_ready, end_server = FALSE;
   int    close_conn;
   char   buffer[80];
   struct sockaddr_in6 addr;
   struct timeval      timeout;
   fd_set              master_set, working_set, write_src_set, temp_write_set;

   // ignore signal
   signal(SIGPIPE, SIG_IGN);

   /*************************************************************/
   /* Create an AF_INET6 stream socket to receive incoming      */
   /* connections on                                            */
   /*************************************************************/
//    listen_sd = socket(AF_INET6, SOCK_STREAM, 0);
    listen_sd = make_socket(SERVER_PORT, on, 0);
    if (listen_sd < 0)
    {
        LogHelper::log(Error, "Server fail to make listen socket");
        return -1;
    }

   

    

   /*************************************************************/
   /* Set socket to be nonblocking. All of the sockets for      */
   /* the incoming connections will also be nonblocking since   */
   /* they will inherit that state from the listening socket.   */
   /*************************************************************/

#ifdef NON_BLOCKING
   rc = ioctl(listen_sd, FIONBIO, (char *)&on);
   if (rc < 0)
   {
      LogHelper::log(Error, "failed to ioctl()");
      close(listen_sd);
      return -2;
   }
#endif

    /*************************************************************/
    /* Bind the socket                                           */
    /*************************************************************/
    // memset(&addr, 0, sizeof(addr));
    // addr.sin6_family      = AF_INET6;
    // memcpy(&addr.sin6_addr, &in6addr_any, sizeof(in6addr_any));
    // addr.sin6_port        = htons(SERVER_PORT);
    // rc = bind(listen_sd,
    //             (struct sockaddr *)&addr, sizeof(addr));
    // if (rc < 0)
    // {
    //     perror("bind() failed");
    //     close(listen_sd);
    //     exit(-1);
    // }

    /*************************************************************/
    /* Set the listen back log                                   */
    /*************************************************************/
    rc = listen(listen_sd, 1024);
    if (rc < 0)
    {
        perror("listen() failed");
        close(listen_sd);
        exit(-1);
    }

    /*************************************************************/
    /* Initialize the master fd_set                              */
    /*************************************************************/
    FD_ZERO(&master_set);
    max_read_sd = listen_sd;
    FD_SET(listen_sd, &master_set);

    FD_ZERO(&write_src_set);
    max_write_sd = 0;
    max_all_sd = max_read_sd;

    /*************************************************************/
    /* Initialize the timeval struct to 3 minutes.  If no        */
    /* activity after 3 minutes this program will end.           */
    /*************************************************************/
    timeout.tv_sec  = 3 * 60;
    timeout.tv_usec = 0;

    ClientMap clientMap;
    HostMap h2lMap;
    LogHelper::log(Info, "Begin loop");
    /*************************************************************/
    /* Loop waiting for incoming connects or for incoming data   */
    /* on any of the connected sockets.                          */
    /*************************************************************/
    do
    {
        /**********************************************************/
        /* Copy the master fd_set over to the working fd_set.     */
        /**********************************************************/
        memcpy(&working_set, &master_set, sizeof(master_set));
        memcpy(&temp_write_set, &write_src_set, sizeof(write_src_set));

        /**********************************************************/
        /* Call select() and wait 3 minutes for it to complete.   */
        /**********************************************************/
        // printf("Waiting on select()...\n");
        LogHelper::log(Debug, "Waiting on select()...\n");
        rc = select(max_all_sd + 1, &working_set, &temp_write_set, NULL, &timeout);

        /**********************************************************/
        /* Check to see if the select call failed.                */
        /**********************************************************/
        if (rc < 0)
        {
            perror("  select() failed");
            break;
        }

        /**********************************************************/
        /* Check to see if the 3 minute time out expired.         */
        /**********************************************************/
        if (rc == 0)
        {
            // printf("  select() timed out. \n");
            LogHelper::log(Info, "  select() timed out. \n");
            // break;
            continue;
        }

        /**********************************************************/
        /* One or more descriptors are readable.  Need to         */
        /* determine which ones they are.                         */
        /**********************************************************/
        desc_ready = rc;
        for (i=0; i <= max_all_sd  &&  desc_ready > 0; ++i)
        {
            /*******************************************************/
            /* Check to see if this descriptor is ready            */
            /*******************************************************/
            if (FD_ISSET(i, &working_set))
            {
                /****************************************************/
                /* A descriptor was found that was readable - one   */
                /* less has to be looked for.  This is being done   */
                /* so that we can stop looking at the working set   */
                /* once we have found all of the descriptors that   */
                /* were ready.                                      */
                /****************************************************/
                desc_ready -= 1;

                /****************************************************/
                /* Check to see if this is the listening socket     */
                /****************************************************/
                if (i == listen_sd)
                {
                    // printf("  Listening socket is readable\n");
                    LogHelper::log(Debug, "  Listening socket is readable\n");
                    /*************************************************/
                    /* Accept all incoming connections that are      */
                    /* queued up on the listening socket before we   */
                    /* loop back and call select again.              */
                    /*************************************************/
    #ifdef NON_BLOCKING
                    do
    #endif
                    {
                        /**********************************************/
                        /* Accept each incoming connection.  If       */
                        /* accept fails with EAGAIN, then we     */
                        /* have accepted all of them.  Any other      */
                        /* failure on accept will cause us to end the */
                        /* server.                                    */
                        /**********************************************/
                        new_sd = accept(listen_sd, NULL, NULL);
                        if (new_sd < 0)
                        {
                            if (errno != EAGAIN)
                            {
                                // perror("  accept() failed");
                                LogHelper::log(Error, "accept failed, %s", strerror(errno));
                                end_server = TRUE;
                            }
                            #ifdef NON_BLOCKING
                            break;
                            #endif
                        }

                        /**********************************************/
                        /* Add the new incoming connection to the     */
                        /* master read set                            */
                        /**********************************************/
                        // printf("  New incoming connection - %d\n", new_sd);
                        LogHelper::log(Info, "  New incoming client connection - %d\n", new_sd);
                        FD_SET(new_sd, &master_set);
                        if (new_sd > max_read_sd)
                            max_read_sd = new_sd;
                        if (new_sd > max_all_sd)
                            max_all_sd = new_sd;
                        
                        rc = serverHandShake(new_sd, clientMap);
                        if (rc < 0) {
                            LogHelper::log(Error, "Fail to handshake");
                            eraseClientFromFdSet(new_sd, master_set,  max_read_sd, max_write_sd, max_all_sd);
                            
                            delete clientMap[new_sd];
                            clientMap.erase(new_sd);
                        }
                        /**********************************************/
                        /* Loop back up and accept another incoming   */
                        /* connection                                 */
                        /**********************************************/
                    }
    #ifdef NON_BLOCKING 
                    while (new_sd != -1);
    #endif
                }

                /****************************************************/
                /* This is not the listening socket, therefore an   */
                /* existing connection must be readable             */
                /****************************************************/
                else if (clientMap.find(i) != clientMap.end())
                {
                    // printf("  Descriptor %d is readable\n", i);
                    LogHelper::log(Debug, "  ClientFd %d is readable\n", i);
                    close_conn = FALSE;
                    /*************************************************/
                    /* Receive all incoming data on this socket      */
                    /* before we loop back and call select again.    */
                    /*************************************************/
    // #ifdef NON_BLOCKING
    //                 do
    // #endif
                    {
                        /**********************************************/
                        /* Receive data on this connection until the  */
                        /* recv fails with EAGAIN.  If any other */
                        /* failure occurs, we will close the          */
                        /* connection.                                */
                        /**********************************************/
                        // rc = recv(i, buffer, sizeof(buffer), 0);
                        // rc = serverHandShake(i, clientMap);
                        // static char readBuf[65536];
                        // memset(readBuf, 0, sizeof(readBuf));
                        // rc = recvData(i, (uint8_t *)readBuf, sizeof(readBuf), clientMap);
                        rc = recvAndHandleClientData(i, clientMap, h2lMap, master_set, write_src_set, max_read_sd, max_write_sd, max_all_sd);
                        if (rc < 0)
                        {
                            // #ifdef NON_BLOCKING
                            // if (errno != EAGAIN)
                            // {
                            // perror("  recv() failed");
                            // close_conn = TRUE;
                            // }
                            
                            // break;
                            // #endif
                            LogHelper::log(Warn, "Server fail to recv data from %d", i);
                            close_conn = TRUE;
                            
                        }

                        /**********************************************/
                        /* Check to see if the connection has been    */
                        /* closed by the client                       */
                        /**********************************************/
                        if (rc >= 0)
                        {
                            LogHelper::log(Debug, "Server succeed in recv and Handle packet from client");
                            // LogHelper::log(Debug, "Recv msg from client: %s", readBuf);
                            // static char sendBuf[] = "Roger that.";
                            // ssize_t sendCode = sendData(i, (uint8_t*)sendBuf, sizeof(sendBuf), clientMap);
                            // if (sendCode < 0) {
                            //     LogHelper::log(Warn, "Server fail to send data to %d", i);
                            //     close_conn = TRUE;
                            // }
                            // printf("  Connection closed\n");
                            // close_conn = TRUE;
                            // #ifdef NON_BLOCKING
                            // break;
                            // #endif
                        }

                        /**********************************************/
                        /* Data was received                          */
                        /**********************************************/
                        // len = rc;
                        // printf("  %d bytes received\n", len);

                        /**********************************************/
                        /* Echo the data back to the client           */
                        /**********************************************/
                        // rc = send(i, buffer, len, 0);
                        // if (rc < 0)
                        // {
                        //     perror("  send() failed");
                        //     close_conn = TRUE;
                        //     #ifdef NON_BLOCKING
                        //     break;
                        //     #endif
                        // }

                    } 
    // #ifdef NON_BLOCKING
    //                 while (TRUE);
    // #endif

                    /*************************************************/
                    /* If the close_conn flag was turned on, we need */
                    /* to clean up this active connection.  This     */
                    /* clean up process includes removing the        */
                    /* descriptor from the master set and            */
                    /* determining the new maximum descriptor value  */
                    /* based on the bits that are still turned on in */
                    /* the master set.                               */
                    /*************************************************/
                    if (close_conn)
                    {
                        // close(i);
                        // FD_CLR(i, &master_set);
                        // delete clientMap[i];
                        // clientMap.erase(i);
                        // if (i == max_read_sd)
                        // {
                        //     while (FD_ISSET(max_read_sd, &master_set) == FALSE)
                        //         max_read_sd -= 1;
                        //     max_all_sd = std::max(max_read_sd, max_write_sd);
                        // }

                        cleanDeadLocalFds(i, h2lMap, clientMap[i]->sMap, master_set, write_src_set, max_read_sd, max_write_sd, max_all_sd, true);
                        delete clientMap[i];
                        clientMap.erase(i);
                        eraseClientFromFdSet(i, master_set,  max_read_sd, max_write_sd, max_all_sd);
        
                    }
                }
                else if(h2lMap.find(i) != h2lMap.end()) {
                    bool shouldCloseHost = false, shouldCloseClient = false;
                    int clientSd = h2lMap[i].clientFd, localFd = h2lMap[i].localFd;
                    static uint8_t readBuf[constants::AES_MAX_DATA_LENGTH - 36];
                    memset(readBuf, 0, sizeof(readBuf));
                    hton_copy4bytes(readBuf, &localFd);
                    rc = recv(i, readBuf + sizeof(localFd), sizeof(readBuf) - sizeof(localFd), 0);
                    if (rc < 0) {
                        if (errno != EAGAIN) {
                            LogHelper::log(Warn, "Failed to recv data from host: %d", i);
                            shouldCloseHost = true;
                        }
                    }
                    if (rc == 0) {
                        LogHelper::log(Warn, "Host %d has been closed", i);
                        shouldCloseHost = true;
                    }
                    else {
                        
                        int sendCode = sendData(clientSd, readBuf, rc + 4, clientMap, constants::SocksTrafficReplyMsg);
                        if (sendCode < 0) {
                            LogHelper::log(Error, "Failed to send reply data to client: %d", clientSd);
                            shouldCloseHost = true;
                            shouldCloseClient = true;
                        }
                        
                    }
                    if (shouldCloseHost) {
                        LogHelper::log(Debug, "Close hostFd: %d, clientSd: %d localFd: %d", i, clientSd, localFd);
                        h2lMap.erase(i);
                        clientMap[clientSd]->sMap.erase(localFd);
                        eraseHostFromFdSets(i, master_set, write_src_set, max_read_sd, max_write_sd, max_all_sd);
                        
                    }
                    if (shouldCloseClient) {
                        LogHelper::log(Debug, "Close client: %d", clientSd);
                        cleanDeadLocalFds(clientSd, h2lMap, clientMap[i]->sMap, master_set, write_src_set, max_read_sd, max_write_sd, max_all_sd, true);
                        eraseClientFromFdSet(clientSd, master_set,  max_read_sd, max_write_sd, max_all_sd);
                        delete clientMap[i];
                        clientMap.erase(i);
                        
                    }
                }
                else {
                    LogHelper::log(Error, "not know what fd is %d", i);
                }
                 /* End of existing connection is readable */
            } /* End of if (FD_ISSET(i, &working_set)) */

            if (FD_ISSET(i, &temp_write_set)) {
                
                LogHelper::log(Debug, "fd : %d is writable", i);

                desc_ready--;
                bool shouldCloseHost = false;
                if (h2lMap.find(i) != h2lMap.end()) {
                    if (h2lMap[i].writeBuf.size() > 0) {
                        static uint8_t writeBuf[constants::PACKET_BUFFER_SIZE];
                        memset(writeBuf, 0, sizeof(writeBuf));
                        ssize_t writeLen = h2lMap[i].readFromBuffer(writeBuf, sizeof(writeBuf));
                        int sendLen = send(i, writeBuf, writeLen, 0);
                        if (sendLen < 0) {
                            if (errno != EAGAIN) {
                                LogHelper::log(Warn, "Failed to send data to host: %d", i);
                                shouldCloseHost = true;
                            }
                        }
                        else if (sendLen > 0) {
                            h2lMap[i].popBuffer(sendLen);
                            LogHelper::log(Debug, "Send :%d bytes to host: %d", sendLen, i);
                        }
                    }
                }
                else {
                    LogHelper::log(Error, "Not know write host_fd, %d", i);
                    // shouldCloseHost = true;
                }

                if (shouldCloseHost) {
                    LogHelper::log(Debug, "Close host fd: %d", i);
                    eraseHostFromFdSets(i, master_set, write_src_set, max_read_sd, max_write_sd, max_all_sd);
                    
                    int localFd = h2lMap[i].localFd, clientFd = h2lMap[i].clientFd;
                    clientMap[clientFd]->sMap.erase(localFd);
                    h2lMap.erase(i);
                }
            }
        } /* End of loop through selectable descriptors */

        cleanDeadClients(clientMap, h2lMap, master_set, write_src_set, max_read_sd, max_write_sd, max_all_sd); 
        cleanDeadHosts(clientMap, h2lMap, master_set, write_src_set, max_read_sd, max_write_sd, max_all_sd);

    } while (end_server == FALSE);

    /*************************************************************/
    /* Clean up all of the sockets that are open                 */
    /*************************************************************/
    for (i=0; i <= max_read_sd; ++i)
    {
        if (FD_ISSET(i, &master_set))
            close(i);
    }
    for (int i = 0; i <= max_write_sd; ++i) {
        if (FD_ISSET(i, &write_src_set))
            close(i);
    }
    return 0;
}