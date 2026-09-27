#include <iostream>
#include <vector>
#include <arpa/inet.h>
#include <sys/socket.h>

using namespace std;

extern const unsigned char RAKNET_MAGIC[16];

// function from reply file
extern void sendOpenConnectionReplyOne(int sock, sockaddr_in clientAddr, unsigned char protocol);

// Decode Open Connection Request 1 (0x05)
void decodeOpenConnectionRequestOne(int sock, sockaddr_in clientAddr, const vector<unsigned char>& data) {

    //cout << "\n🔓 [Open Connection Request 1 Detected]\n";

    if (data.size() < 1 + 16 + 1) {
        cout << "[WARN] Invalid packet size for Open Connection Request 1.\n";
        return;
    }

    // Verify RakNet magic
    bool magicOK = true;
    for (int i = 0; i < 16; ++i) {
        if (data[1 + i] != RAKNET_MAGIC[i])
            magicOK = false;
    }

    unsigned char protocol = data[17];

    // Diagnostic logging (was commented out): shows whether the client's
    // magic matched (a mismatch means NO reply is sent) and which RakNet
    // protocol version byte this client declares (real Bedrock uses 11).
    cout << "[RakNet] OCR1 from " << inet_ntoa(clientAddr.sin_addr)
         << ":" << ntohs(clientAddr.sin_port)
         << " size=" << data.size()
         << " raknetProtocol=" << (int)protocol
         << " magic=" << (magicOK ? "OK" : "BAD -- NOT replying") << "\n";

    if (magicOK)
        sendOpenConnectionReplyOne(sock, clientAddr, protocol);
}