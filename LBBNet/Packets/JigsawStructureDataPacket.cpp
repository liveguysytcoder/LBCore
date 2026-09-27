#include "JigsawStructureDataPacket.h"
#include "DataTypeHelper.h"
#include "GamePacketSender.h"
#include <iostream>

using namespace std;

namespace {

// Network-flavor NBT tag name: VarInt length, then raw bytes -- matching
// this project's already-established convention (see StartGamePacket.cpp's
// writeEmptyNbtCompound and ItemRegistryData.cpp's per-item name strings),
// which is DIFFERENT from BlockHash.cpp's classic-LE NBT (u16 length) used
// only for computing block-state hashes. Do not mix the two.
void writeNetworkNbtName(vector<uint8_t>& buf, const string& name) {
    writeVarInt(buf, static_cast<uint32_t>(name.size()));
    buf.insert(buf.end(), name.begin(), name.end());
}

// An empty TAG_List of TAG_Compound: type byte (0x0A), then VarInt count
// (0). Matches Dragonfly's real capture exactly (each of the 4 children
// here decodes as {type:"list", value:{type:"compound", value: []}}).
void writeEmptyCompoundList(vector<uint8_t>& buf, const string& name) {
    buf.push_back(0x09); // TAG_List
    writeNetworkNbtName(buf, name);
    buf.push_back(0x0A); // element type = TAG_Compound
    writeVarInt(buf, 0); // element count = 0
}

} // namespace

void sendJigsawStructureData(int sock, sockaddr_in clientAddr, ClientState& state) {
    vector<uint8_t> packet;
    writeVarInt(packet, 313);

    // Root: unnamed TAG_Compound containing 4 empty TAG_List<TAG_Compound>
    // children, in the same order as the real Dragonfly capture.
    packet.push_back(0x0A); // TAG_Compound
    writeVarInt(packet, 0); // root name length = 0 (unnamed root tag)

    writeEmptyCompoundList(packet, "processors");
    writeEmptyCompoundList(packet, "template_pools");
    writeEmptyCompoundList(packet, "jigsaws");
    writeEmptyCompoundList(packet, "structure_sets");

    packet.push_back(0x00); // TAG_End -- closes the root compound

    queueGamePacket(state, packet);
    cout << "[Bedrock] Sent JigsawStructureData (empty)\n";
}
