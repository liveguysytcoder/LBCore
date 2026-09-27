#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <optional>

using namespace std;

// Stage 2 of real vanilla-world compatibility: decoding a real
// SubChunkPrefix record's value (see RealChunkKeys.h for the key that
// points to one of these) into an actual 16x16x16 grid of real block
// names.
//
// Format confirmed from Tomcc's (Mojang) own original block-storage spec
// (gist.github.com/Tomcc/a96af509e275b1af483b25c543cfbf37), PLUS several
// real corrections made in that gist's own comment thread by people who
// hit them implementing real parsers -- both incorporated here, not just
// the original doc:
//   - The header byte is (bitsPerBlock << 1) | isRuntimeFlag, NOT the
//     reverse order the original doc text described -- confirmed by
//     @7kasper's 2018 correction.
//   - Version 9 (not just 8) exists and adds an extra sub-chunk-index
//     byte after the storage count -- confirmed by @gentlegiantJGC/
//     @AncientHello/@fahad-cpp across 2021-2026.
//   - The persistence (on-disk) palette's entry COUNT is a raw 4-byte
//     little-endian int, NOT a varint like the original doc claimed --
//     confirmed independently by @geNAZt and @dktapps in 2018 (varint
//     there crashes real clients/misreads real files; a plain int
//     doesn't). This project's own encoder (LevelChunkPacket.cpp's
//     appendSuperflatSubchunk) already used the corrected
//     (bitsPerBlock<<1)|flag header convention and the same LSB-first bit
//     shift direction this decoder uses -- both match this reference
//     independently, which is a good cross-check.
//   - Persistence palette entries are classic-LE NBT compounds (same
//     format as level.dat -- see NbtClassic.h), each describing one
//     block's name (and historically a "val" variant number; modern
//     worlds instead carry a "states" compound of individual block state
//     key/value pairs, which this Stage 2 decoder does not yet expose --
//     only the block "name" is captured, which is enough to prove real
//     decoding works and is enough input for Stage 3's hashing, though
//     Stage 3 will eventually want the actual states too for full
//     accuracy on blocks whose visual/behavior varies by state).
//
// NOT implemented: subchunk format versions 0-7 (the pre-1.2.13 flat
// block-array format) or version 1 (single implicit block storage, no
// count/index bytes) -- real modern (post-1.2.13) worlds use version 8 or
// 9, which covers what a real downloaded current-day map will contain.

struct DecodedSubChunk {
    bool valid = false;
    uint8_t subChunkIndex = 0; // from the key, not the value -- caller fills this in
    // 4096 entries, one per block position, packed as
    // index = (x * 16 + z) * 16 + y  -- the SAME position formula the
    // reference pseudocode uses (x<<8 | z<<4 | y), so this lines up
    // directly with this project's own existing XZY iteration order in
    // appendSuperflatSubchunk.
    vector<string> blockNames;
};

// Decodes one SubChunkPrefix record's raw value (as read straight out of
// leveldb — see WorldStorage.h/RealChunkKeys.h for how to get one).
// Returns nullopt if the version byte isn't 8 or 9, or if the data is
// truncated/malformed partway through parsing -- both are reported by
// the caller (see scanAndReportWorldChunkKeys in RealChunkKeys.cpp)
// rather than treated as a hard error, since a real world can validly
// contain some subchunks in older formats this decoder doesn't handle.
optional<DecodedSubChunk> decodeSubChunkPrefix(const vector<uint8_t>& rawValue);
