#include "DisconnectPacket.h"
#include "DataTypeHelper.h"
#include "GamePacketSender.h"
#include "../../LBNet/Server/Server.h"
#include <iostream>

using namespace std;

void sendDisconnect(int sock, sockaddr_in clientAddr, ClientState& state,
                    int32_t reason, const string& message, bool hideScreen) {
    vector<uint8_t> packet;
    writeVarInt(packet, 5);           // Disconnect
    writeZigZag32(packet, reason);    // reason (varint32)
    writeBool(packet, hideScreen);    // hide disconnection screen
    if (!hideScreen) {
        writeString(packet, message); // message
        writeString(packet, message); // filtered message
    }
    queueGamePacket(state, packet);
    flushGamePackets(sock, clientAddr, state);
    cout << "[Bedrock] Sent Disconnect(reason=" << reason << ", \"" << message << "\")\n";
}

void disconnectAllClients(int32_t reason, const string& message) {
    if (serverSocket < 0) return;
    int sock = serverSocket;
    forEachClient([&](ClientState& c) {
        if (c.loggedIn) sendDisconnect(sock, c.addr, c, reason, message, false);
    });
}
