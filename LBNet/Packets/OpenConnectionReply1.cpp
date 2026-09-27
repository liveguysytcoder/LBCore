#include <iostream>
#include <cerrno>
#include <cstring>
#include <vector>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "RaknetGlobals.h"

using namespace std;

// Send Open Connection Reply 1 (0x06)
void sendOpenConnectionReplyOne(int sock, sockaddr_in clientAddr, unsigned char protocol) {

    vector<unsigned char> reply;

    reply.push_back(0x06);

    // RakNet magic
    reply.insert(reply.end(), RAKNET_MAGIC, RAKNET_MAGIC + 16);

    // Server GUID
    for (int i = 7; i >= 0; --i)
        reply.push_back((SERVER_GUID >> (i * 8)) & 0xFF);

    reply.push_back(0x00); // security disabled

    unsigned short mtu = 1492;
    reply.push_back((mtu >> 8) & 0xFF);
    reply.push_back(mtu & 0xFF);

    ssize_t sent = sendto(sock, reply.data(), reply.size(), 0,
           (struct sockaddr*)&clientAddr, sizeof(clientAddr));

    // Diagnostic logging: proves whether the reply actually left the socket.
    if (sent < 0) {
        cout << "[RakNet] Reply1 sendto FAILED: " << strerror(errno) << "\n";
    } else {
        cout << "[RakNet] Reply1 sent to " << inet_ntoa(clientAddr.sin_addr)
             << ":" << ntohs(clientAddr.sin_port)
             << " (" << sent << " bytes, mtu " << mtu << ")\n";
    }

    //cout << "📤 Sent Open Connection Reply 1 (" << reply.size() << " bytes)\n";
}
