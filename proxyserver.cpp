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
#include "proxyserver.h"
#include "constants.h"
#include "utils.h"

#include <unordered_map>

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

struct HostNode {
    int clientFd, localFd;
    HostNode(int clientFd, int localFd, time_t lastTime): clientFd(clientFd), localFd(localFd), lastTime(lastTime) {}
    HostNode(): clientFd(0), localFd(0), lastTime(0) {}
    time_t lastTime;
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



int recvAndHandleClientData(int sockFd, ClientMap &cMap, HostMap &h2lMap, fd_set &fdSet, int &max_sd) {
    static uint8_t buf[constants::MAX_MSG_DATA_LENGTH];
    memset(buf, 0, sizeof(buf));
    uint8_t msgType = 0;
    int recvLen = recvData(sockFd, buf, sizeof(buf), cMap, &msgType);
    if (recvLen < 0) {
        LogHelper::log(Error, "Failed to recv data from client: %d", sockFd);
        return recvLen;
    }
    using namespace constants;
    SocketMap *l2hMap = &(cMap[sockFd]->sMap);
    if (msgType == constants::SocksTcpRequestMsg) {
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
                
                
                int hostSd = tryConnectSocket(dstAddrBuf, dstPort, constants::serverNonBlocking);
                if (hostSd < 0) {
                    respCode = 0xff;
                }
                else {
                    time_t nowT = time(NULL);
                    (*l2hMap)[localFd] = SocketNode(hostSd, nowT);
                    h2lMap[hostSd] = HostNode(sockFd, localFd, nowT);
                    FD_SET(hostSd, &fdSet);
                    if (hostSd > max_sd) {
                        max_sd = hostSd;
                    }
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
        
        int localFd = -1;
        if (recvLen < sizeof(localFd)) {
            LogHelper::log(Error, "Fail to receive socks traffic request, length less than localFd");
            return -5;
        }
        ntoh_copy4bytes(&localFd, buf);
        auto l2hIt = l2hMap->find(localFd);
        if (l2hIt == l2hMap->end()) {
            LogHelper::log(Error, "Can not find localFd %d in l2hMap", localFd);
            return -6;
        }
        int hostSd = (*l2hMap)[localFd].sockFd;
        ssize_t sendCode = writeNBytes(hostSd, buf + sizeof(localFd), recvLen - sizeof(localFd), constants::serverNonBlocking);
        if (sendCode < 0) {
            LogHelper::log(Warn, "Failed to send data to host, localFd: %d, will close hostSd", localFd);
            close(hostSd);
            FD_CLR(hostSd, &fdSet);
            if (hostSd == max_sd) {
                while (FD_ISSET(max_sd, &fdSet) == 0)
                    max_sd -= 1;
            }
            l2hMap->erase(localFd);
            h2lMap.erase(hostSd);
            return -7;
        }
        LogHelper::log(Debug, "End Send %d bytes to host: %d, content: ",recvLen - sizeof(localFd), hostSd);
        for (int k = 0; k < recvLen - sizeof(localFd); ++k) {
            fprintf(stderr, "%c", buf[sizeof(localFd) + k]);
        }
        fprintf(stderr, "\n");
        time_t nowT = time(NULL);
        l2hIt->second.lastTime = nowT;
        h2lMap[hostSd].lastTime = nowT;
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

void cleanDeadSockets(SocketMap &sMap) {
    time_t now = time(NULL);
    for (auto it = sMap.begin(); it != sMap.end();) {
        if (now - it->second.lastTime > constants::SocketTimeOutSeconds) {
            LogHelper::log(Debug, "Clean timeout socket fd: %d", it->first);
            it = sMap.erase(it);
        }
        else it++;
    }
}

void cleanDeadHosts(HostMap &sMap) {
    time_t now = time(NULL);
    for (auto it = sMap.begin(); it != sMap.end();) {
        if (now - it->second.lastTime > constants::SocketTimeOutSeconds) {
            LogHelper::log(Debug, "Clean timeout socket fd: %d", it->first);
            it = sMap.erase(it);
        }
        else it++;
    }
}

void cleanDeadClients(ClientMap &cMap) {
    time_t now = time(NULL);
    for (auto it = cMap.begin(); it != cMap.end(); ) {
        if (now - it->second->lastTime > constants::ClientTimeOutSeconds) {
            LogHelper::log(Debug, "Clean timeout client fd: %d", it->first);
            delete it->second;
            it = cMap.erase(it);
        }
        else {
            cleanDeadSockets(it->second->sMap);
            it++;
        }
    }
}



int launch_server()
{
   int    i, len, rc, on = 1;
   int    listen_sd, max_sd, new_sd;
   int    desc_ready, end_server = FALSE;
   int    close_conn;
   char   buffer[80];
   struct sockaddr_in6 addr;
   struct timeval      timeout;
   fd_set              master_set, working_set;

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
    max_sd = listen_sd;
    FD_SET(listen_sd, &master_set);

    /*************************************************************/
    /* Initialize the timeval struct to 3 minutes.  If no        */
    /* activity after 3 minutes this program will end.           */
    /*************************************************************/
    timeout.tv_sec  = 3 * 60;
    timeout.tv_usec = 0;

    ClientMap clientMap;
    HostMap h2lMap;
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

        /**********************************************************/
        /* Call select() and wait 3 minutes for it to complete.   */
        /**********************************************************/
        printf("Waiting on select()...\n");
        rc = select(max_sd + 1, &working_set, NULL, NULL, &timeout);

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
            printf("  select() timed out. \n");
            // break;
            continue;
        }

        /**********************************************************/
        /* One or more descriptors are readable.  Need to         */
        /* determine which ones they are.                         */
        /**********************************************************/
        desc_ready = rc;
        for (i=0; i <= max_sd  &&  desc_ready > 0; ++i)
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
                    printf("  Listening socket is readable\n");
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
                        printf("  New incoming connection - %d\n", new_sd);
                        FD_SET(new_sd, &master_set);
                        if (new_sd > max_sd)
                            max_sd = new_sd;
                        
                        rc = serverHandShake(new_sd, clientMap);
                        if (rc < 0) {
                            LogHelper::log(Error, "Fail to handshake");
                            close(new_sd);
                            FD_CLR(new_sd, &master_set);
                            delete clientMap[new_sd];
                            clientMap.erase(new_sd);
                            if (new_sd == max_sd)
                            {
                                while (FD_ISSET(max_sd, &master_set) == FALSE)
                                max_sd -= 1;
                            }
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
                    printf("  Descriptor %d is readable\n", i);
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
                        rc = recvAndHandleClientData(i, clientMap, h2lMap, master_set, max_sd);
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
                        close(i);
                        FD_CLR(i, &master_set);
                        delete clientMap[i];
                        clientMap.erase(i);
                        if (i == max_sd)
                        {
                            while (FD_ISSET(max_sd, &master_set) == FALSE)
                            max_sd -= 1;
                        }
                    }
                }
                else if(h2lMap.find(i) != h2lMap.end()) {
                    bool shouldCloseHost = false, shouldCloseClient = false;
                    int clientSd = h2lMap[i].clientFd, localFd = h2lMap[i].localFd;
                    static uint8_t readBuf[constants::AES_MAX_DATA_LENGTH];
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
                        close(i);
                        FD_CLR(i, &master_set);
                        if (i == max_sd) {
                            while (FD_ISSET(max_sd, &master_set) == 0)
                                max_sd -= 1;
                        }
                    }
                    if (shouldCloseClient) {
                        close(clientSd);
                        FD_CLR(clientSd, &master_set);
                        if (clientSd == max_sd) {
                            while (FD_ISSET(max_sd, &master_set) == 0)
                                max_sd -= 1;
                        }
                    }
                }
                else {
                    LogHelper::log(Error, "not know what fd is %d", i);
                }
                 /* End of existing connection is readable */
            } /* End of if (FD_ISSET(i, &working_set)) */
        } /* End of loop through selectable descriptors */

        cleanDeadClients(clientMap);
        cleanDeadHosts(h2lMap);

    } while (end_server == FALSE);

    /*************************************************************/
    /* Clean up all of the sockets that are open                 */
    /*************************************************************/
    for (i=0; i <= max_sd; ++i)
    {
        if (FD_ISSET(i, &master_set))
            close(i);
    }
    return 0;
}