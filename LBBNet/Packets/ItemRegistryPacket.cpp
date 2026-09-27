#include "ItemRegistryPacket.h"
#include "DataTypeHelper.h"
#include "GamePacketSender.h"
#include <iostream>

using namespace std;

extern const unsigned char kItemRegistryBody[];
extern const size_t kItemRegistryBodyLen;

void sendItemRegistry(int sock, sockaddr_in clientAddr, ClientState& state) {
    // Gamepacket header (varint) for ItemRegistry = 162 (0xA2), followed by
    // the pre-encoded array of all 1934 vanilla items -- see
    // ItemRegistryData.cpp for the real per-item field layout (name,
    // runtime id, component-based flag, version, nbt), confirmed directly
    // against the protocol.json schema rather than gophertunnel's Go
    // struct, which was missing the version+nbt fields for this protocol
    // revision. An empty item list here is what was causing the client to
    // crash right after PlayStatus(PlayerSpawn): it needs this full table
    // to resolve item stacks once it actually spawns in.
    vector<uint8_t> packet;
    writeVarInt(packet, 162);

    packet.insert(packet.end(), kItemRegistryBody, kItemRegistryBody + kItemRegistryBodyLen);

    queueGamePacket(state, packet);
    cout << "[Bedrock] Sent ItemRegistry (" << kItemRegistryBodyLen << " bytes, 1934 items)\n";
}

void sendEmptyItemRegistry(int sock, sockaddr_in clientAddr, ClientState& state) {
    vector<uint8_t> packet;
    writeVarInt(packet, 162); // ItemRegistry
    writeVarInt(packet, 0);   // Item Data: array count = 0 (no custom items)
    queueGamePacket(state, packet);
    cout << "[Bedrock] Sent ItemRegistry (2nd time, empty -- custom items only)\n";
}
