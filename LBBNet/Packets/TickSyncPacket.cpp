#include "TickSyncPacket.h"
#include "DataTypeHelper.h"
#include "GamePacketSender.h"
#include <iostream>

using namespace std;

void handleTickSync(int sock, sockaddr_in clientAddr, ClientState& state,
                     const vector<uint8_t>& data, size_t& offset) {
    int64_t requestTime = readInt64(data, offset);
    (void)readInt64(data, offset); // response_time -- always 0 on the client's outgoing request, nothing to read here

    cout << "[Bedrock] TickSync request_time=" << requestTime << " -- replying\n";

    // Gamepacket header (varint) for TickSync = 23 (0x17).
    vector<uint8_t> packet;
    writeVarInt(packet, 23);
    writeInt64(packet, requestTime); // request_time -- echoed back unchanged
    writeInt64(packet, requestTime); // response_time -- see TickSyncPacket.h for why an echo instead of a real tick count

    sendGamePacket(sock, clientAddr, state, packet);
}
