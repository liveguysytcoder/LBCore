#include "SubChunkPacket.h"
#include "DataTypeHelper.h"
#include "GamePacketSender.h"
#include "LevelChunkPacket.h"
#include <iostream>

using namespace std;

// The sub-chunk / biome serialisers now live in LevelChunkPacket.cpp and are
// shared: this file used to carry its own copies with the same defects (no Y
// index byte, plain-varint palette count, biome data wrongly appended to a
// SubChunk payload -- biomes travel in LevelChunk, never in SubChunk).

// SubChunk (id 174 -- NOT 161; the previous value was wrong). Layout verified
// against gophertunnel v1.62.0 (protocol 2193) SubChunk/SubChunkEntry.Marshal():
//   cacheEnabled bool, dimension varint32, position = THREE FIXED int32 (not
//   varints), entries: varuint count, then per entry:
//     offset (3 x int8), result u8, rawPayload (optional: bool + varuint
//     length + bytes), heightMapType u8, heightMap (optional: bool [+ 16 rows
//     of varuint 16 + 16 x int8]), renderHeightMapType u8, renderHeightMap
//     (optional), blobHash (optional u64)
// The old code wrote heightMapType BEFORE the payload, had no presence
// bytes, no render height-map fields, and used varints for the position.
void sendSubChunkResponse(int sock, sockaddr_in clientAddr, ClientState& state,
                           int32_t dimension, int32_t baseX, int32_t baseY, int32_t baseZ,
                           const vector<SubChunkOffsetRequest>& offsets) {
    (void)sock; (void)clientAddr;
    vector<uint8_t> packet;
    writeVarInt(packet, 174); // SubChunk

    writeBool(packet, false);         // CacheEnabled
    writeZigZag32(packet, dimension); // dimension (varint32)
    writeInt(packet, baseX);          // SubChunkPos: fixed int32 x3
    writeInt(packet, baseY);
    writeInt(packet, baseZ);

    writeVarInt(packet, (uint32_t)offsets.size());
    for (const auto& off : offsets) {
        writeByte(packet, off.dx);
        writeByte(packet, off.dy);
        writeByte(packet, off.dz);
        writeUByte(packet, 1);        // Result: Success

        // Absolute sub-chunk index = requested base + this offset. The Y index
        // byte inside the serialised sub-chunk must be this absolute value.
        const int32_t absY = baseY + static_cast<int32_t>(off.dy);
        vector<uint8_t> subPayload;
        if (absY == kTerrainSubChunkIndex) appendTerrainSubChunk(subPayload, static_cast<int8_t>(absY));
        else                               appendEmptySubChunk(subPayload, static_cast<int8_t>(absY));

        writeBool(packet, true);                          // rawPayload present
        writeVarInt(packet, (uint32_t)subPayload.size()); // byte-slice length
        packet.insert(packet.end(), subPayload.begin(), subPayload.end());

        writeUByte(packet, 0);        // heightMapType: none
        writeBool(packet, false);     // heightMap: absent
        writeUByte(packet, 0);        // renderHeightMapType: none
        writeBool(packet, false);     // renderHeightMap: absent
        writeBool(packet, false);     // blobHash: absent
    }

    queueGamePacket(state, packet);
    cout << "[Bedrock] Sent SubChunk response (" << offsets.size() << " offset(s), base=("
         << baseX << "," << baseY << "," << baseZ << "))\n";
}
