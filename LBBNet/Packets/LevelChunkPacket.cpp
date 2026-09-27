#include "LevelChunkPacket.h"
#include "DataTypeHelper.h"
#include "GamePacketSender.h"
#include "BlockHash.h"
#include "../World/WorldStorage.h"
#include <iostream>

using namespace std;

// ---------------------------------------------------------------------------
// How terrain reaches the client, and what was wrong before
//
// This packet used to send a "request mode" header (sub_chunk_count = 0 plus a
// present SubChunkLimit) while putting the terrain INLINE in the payload. Those
// contradict each other: request mode means "the client will now send
// SubChunkRequest packets, and this payload holds only biomes". The earlier
// conclusion "the client never requests sub-chunks, so terrain must be inline"
// came from a capture made with a bot client -- a bot never renders, so it
// never sends requests. A real client does, and would find sub-chunk bytes
// where it expected biome data.
//
// The serialisers were also malformed for the real client (checked by running
// a port of Dragonfly's chunk decoder, which supports 1.26.50, over the bytes):
//   * version-9 sub-chunks need a Y-index byte after the storage count;
//   * the palette entry count is a ZIGZAG varint, and is omitted entirely for a
//     0-bit (uniform) storage;
//   * the biome column is one storage per sub-chunk section (24 in the
//     overworld), not a single one.
// Chunks are now sent INLINE (header: sub_chunk_count = N, no SubChunkLimit),
// the same way PocketMine does. Payload layout:
//   N x sub-chunk (lowest index first) | 24 x biome storage | border count (0)
// ---------------------------------------------------------------------------

void appendEmptySubChunk(vector<uint8_t>& buf, int8_t yIndex) {
    buf.push_back(9);                          // sub-chunk version
    buf.push_back(0);                          // no storage layers: all air
    buf.push_back(static_cast<uint8_t>(yIndex));
}

void appendTerrainSubChunk(vector<uint8_t>& buf, int8_t yIndex) {
    buf.push_back(9);                          // sub-chunk version
    buf.push_back(1);                          // storage layer count
    buf.push_back(static_cast<uint8_t>(yIndex)); // Y index (signed sub-chunk coordinate)

    const int bitsPerBlock = 2;
    const int blocksPerWord = 32 / bitsPerBlock; // 16
    const int totalBlocks = 16 * 16 * 16;         // 4096
    const int wordCount = (totalBlocks + blocksPerWord - 1) / blocksPerWord; // 256

    buf.push_back(static_cast<uint8_t>((bitsPerBlock << 1) | 1)); // storage header (network flag set)

    vector<uint8_t> indices;
    indices.reserve(totalBlocks);
    for (int x = 0; x < 16; x++) {
        for (int z = 0; z < 16; z++) {
            for (int y = 0; y < 16; y++) {
                uint8_t paletteIndex;
                if (y == 0)       paletteIndex = 1; // bedrock
                else if (y <= 2)  paletteIndex = 2; // dirt
                else if (y == 3)  paletteIndex = 3; // grass
                else              paletteIndex = 0; // air
                indices.push_back(paletteIndex);
            }
        }
    }

    for (int w = 0; w < wordCount; w++) {
        uint32_t word = 0;
        for (int j = 0; j < blocksPerWord; j++) {
            int pos = w * blocksPerWord + j;
            uint32_t v = (pos < totalBlocks) ? indices[pos] : 0;
            word |= (v & ((1u << bitsPerBlock) - 1)) << (j * bitsPerBlock);
        }
        buf.push_back(word & 0xFF);
        buf.push_back((word >> 8) & 0xFF);
        buf.push_back((word >> 16) & 0xFF);
        buf.push_back((word >> 24) & 0xFF);
    }

    int32_t airHash     = computeBlockHash("minecraft:air", {});
    // infiniburn_bit is a Boolean state (TAG_Byte in NBT, not TAG_Int), which
    // changes the exact bytes hashed -- see the note in BlockHash.h.
    int32_t bedrockHash = computeBlockHash("minecraft:bedrock", {{"infiniburn_bit", BlockStateValue::ofByte(0)}});
    int32_t dirtHash    = computeBlockHash("minecraft:dirt", {});
    int32_t grassHash   = computeBlockHash("minecraft:grass_block", {});

    writeZigZag32(buf, 4); // palette entry count: ZIGZAG varint (was a plain varint)
    writeZigZag32(buf, airHash);
    writeZigZag32(buf, bedrockHash);
    writeZigZag32(buf, dirtHash);
    writeZigZag32(buf, grassHash);
}

void appendBiomeColumn(vector<uint8_t>& buf) {
    // First storage: uniform (0 bits per entry) with the network flag set. A
    // 0-bit storage has no index words and NO palette count -- just the value.
    buf.push_back(static_cast<uint8_t>((0 << 1) | 1));
    // Biome id 1 = plains (legacy numeric id), as a ZIGZAG varint.
    writeZigZag32(buf, 1);
    // Every other section: "same as the previous storage" (0x7F << 1 | 1).
    for (int i = 1; i < kOverworldSubChunkSections; i++) buf.push_back(0xFF);
}

namespace {
// Chunks saved by earlier versions are in the old, malformed layout, which
// begins 09 01 05 (one populated sub-chunk with no Y index). Every payload
// written by this version begins with the empty sub-chunk for the lowest index
// (09 00 FC), so that is used to recognise -- and discard -- stale saves
// instead of sending the client broken data forever.
bool isCurrentPayloadFormat(const vector<uint8_t>& p) {
    return p.size() >= 3 && p[0] == 9 && p[1] == 0 &&
           p[2] == static_cast<uint8_t>(kLowestSubChunkIndex);
}
} // namespace

void sendLevelChunk(int sock, sockaddr_in clientAddr, ClientState& state, int32_t chunkX, int32_t chunkZ) {
    (void)sock; (void)clientAddr;
    vector<uint8_t> packet;
    writeVarInt(packet, 58); // LevelChunk

    writeZigZag32(packet, chunkX);
    writeZigZag32(packet, chunkZ);
    writeZigZag32(packet, 0);                      // Dimension: 0 = overworld
    writeVarInt(packet, kInlineSubChunkCount);     // sub-chunks carried in the payload
    writeBool(packet, false);                      // SubChunkLimit absent => INLINE mode
    writeBool(packet, false);                      // CacheEnabled -- no blob-cache transaction
    writeVarInt(packet, 0);                        // blob hash count (cache disabled)

    vector<uint8_t> rawPayload;
    // Persistence: reuse this chunk's saved terrain if it is in the current
    // format; otherwise (nothing saved, or a stale save from an older
    // version) generate it and save the fresh copy over the old one.
    bool loaded = tryLoadChunkRaw(chunkX, chunkZ, rawPayload) && isCurrentPayloadFormat(rawPayload);
    if (!loaded) {
        rawPayload.clear();
        for (uint32_t i = 0; i < kInlineSubChunkCount; i++) {
            const int8_t yIndex = static_cast<int8_t>(kLowestSubChunkIndex + static_cast<int>(i));
            if (yIndex == kTerrainSubChunkIndex) appendTerrainSubChunk(rawPayload, yIndex);
            else                                 appendEmptySubChunk(rawPayload, yIndex);
        }
        appendBiomeColumn(rawPayload);
        writeVarInt(rawPayload, 0); // Border blocks (Education Edition only) -- none
        // No block entities -- nothing in this world has one.
        saveChunkRaw(chunkX, chunkZ, rawPayload);
    }

    writeVarInt(packet, static_cast<uint32_t>(rawPayload.size()));
    packet.insert(packet.end(), rawPayload.begin(), rawPayload.end());

    state.sentChunks.insert({chunkX, chunkZ}); // remembered for chunk streaming (Multiplayer.cpp)
    queueGamePacket(state, packet);
    cout << "[Bedrock] Sent LevelChunk(" << chunkX << ", " << chunkZ << ") -- inline, "
         << kInlineSubChunkCount << " sub-chunks + biomes (" << rawPayload.size() << " bytes payload)\n";
}
