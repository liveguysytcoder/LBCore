#include "RealSubChunk.h"
#include "NbtClassic.h"
#include <iostream>

using namespace std;

namespace {

bool readU32LE(const vector<uint8_t>& data, size_t& offset, uint32_t& out) {
    if (offset + 4 > data.size()) return false;
    out = static_cast<uint32_t>(data[offset]) |
          (static_cast<uint32_t>(data[offset + 1]) << 8) |
          (static_cast<uint32_t>(data[offset + 2]) << 16) |
          (static_cast<uint32_t>(data[offset + 3]) << 24);
    offset += 4;
    return true;
}

// Decodes one Block Storage (the [header][words][palette] structure
// nested inside a subchunk -- see RealSubChunk.h for the format sourcing)
// starting at `offset`. Only storage index 0 (the primary block layer) is
// kept; layer 1+ (used for water co-existing with another block, per
// Tomcc's own gist comment) is parsed just enough to skip over correctly,
// not exposed -- Stage 2's goal is proving real terrain decodes, and the
// water-overlay layer isn't needed for that.
bool decodeOneBlockStorage(const vector<uint8_t>& data, size_t& offset,
                            vector<string>* outNames /* nullptr to skip-only */) {
    if (offset >= data.size()) return false;
    uint8_t header = data[offset++];
    bool isRuntime = (header & 1) != 0;
    int bitsPerBlock = header >> 1;

    if (bitsPerBlock == 0) {
        // 0 bits per block: every position is palette entry 0 -- no word
        // array at all, matching this project's own encoder's use of the
        // same special case for uniform sub-chunks (see
        // appendSuperflatSubchunk's air-layer / "uniform value" comments).
        uint32_t paletteCount;
        if (isRuntime) {
            // Runtime palette count would be a signed VarInt -- not
            // expected here (persistence data only), left unhandled.
            return false;
        }
        if (!readU32LE(data, offset, paletteCount)) return false;
        if (paletteCount < 1) return false;

        NbtCompoundView entry;
        if (!readNbtCompoundClassic(data, offset, entry)) return false;
        string name = entry.strings.count("name") ? entry.strings["name"] : "minecraft:unknown";
        if (outNames) outNames->assign(4096, name);
        return true;
    }

    if (bitsPerBlock < 1 || bitsPerBlock > 32) return false; // not a valid Type per the spec table

    int blocksPerWord = 32 / bitsPerBlock;
    int wordCount = (4096 + blocksPerWord - 1) / blocksPerWord;

    size_t wordArrayStart = offset;
    if (offset + (size_t)wordCount * 4 > data.size()) return false;
    offset += (size_t)wordCount * 4;

    if (isRuntime) {
        return false; // persistence-only decoder -- see RealSubChunk.h
    }

    uint32_t paletteCount;
    if (!readU32LE(data, offset, paletteCount)) return false;

    vector<string> paletteNames;
    paletteNames.reserve(paletteCount);
    for (uint32_t i = 0; i < paletteCount; i++) {
        NbtCompoundView entry;
        if (!readNbtCompoundClassic(data, offset, entry)) return false;
        paletteNames.push_back(entry.strings.count("name") ? entry.strings["name"] : "minecraft:unknown");
    }

    if (outNames) {
        outNames->assign(4096, "minecraft:unknown");
        int position = 0;
        for (int w = 0; w < wordCount && position < 4096; w++) {
            size_t wOff = wordArrayStart + (size_t)w * 4;
            uint32_t word = static_cast<uint32_t>(data[wOff]) |
                             (static_cast<uint32_t>(data[wOff + 1]) << 8) |
                             (static_cast<uint32_t>(data[wOff + 2]) << 16) |
                             (static_cast<uint32_t>(data[wOff + 3]) << 24);
            for (int j = 0; j < blocksPerWord && position < 4096; j++) {
                uint32_t index = (word >> (j * bitsPerBlock)) & ((1u << bitsPerBlock) - 1);
                (*outNames)[position] = (index < paletteNames.size())
                                             ? paletteNames[index]
                                             : "minecraft:unknown"; // out-of-range: padding bits, not a real block -- see RealSubChunk.h
                position++;
            }
        }
    }

    return true;
}

} // namespace

optional<DecodedSubChunk> decodeSubChunkPrefix(const vector<uint8_t>& rawValue) {
    if (rawValue.empty()) return nullopt;

    size_t offset = 0;
    uint8_t version = rawValue[offset++];

    if (version != 8 && version != 9) {
        return nullopt; // see RealSubChunk.h -- older formats not implemented yet
    }

    if (offset >= rawValue.size()) return nullopt;
    uint8_t storageCount = rawValue[offset++]; // confirmed: a raw byte, not a varint

    DecodedSubChunk result;
    if (version == 9) {
        if (offset >= rawValue.size()) return nullopt;
        result.subChunkIndex = rawValue[offset++];
    }

    if (storageCount < 1) return nullopt;

    // Storage 0 is the real block layer we want; anything after it
    // (storage 1 = water co-existing with another block) is parsed
    // strictly to keep `offset` correct, not exposed.
    if (!decodeOneBlockStorage(rawValue, offset, &result.blockNames)) return nullopt;
    for (int i = 1; i < storageCount; i++) {
        if (!decodeOneBlockStorage(rawValue, offset, nullptr)) break; // tolerate a malformed extra layer
    }

    result.valid = true;
    return result;
}
