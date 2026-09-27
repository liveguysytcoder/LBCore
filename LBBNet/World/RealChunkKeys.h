#pragma once
#include <string>
#include <cstdint>
#include <optional>

using namespace std;

// Stage 1 of real vanilla-world compatibility: parsing Mojang's actual
// on-disk chunk key format, confirmed against Minecraft Wiki's Bedrock
// Edition level format page ("Chunk key format" section). This is a
// DIFFERENT key scheme from the one WorldStorage.cpp uses for LBCore's
// own generated chunks -- see WorldStorage.h's scope note.
//
// A chunk key is: two int32 LE (x, z) + optional int32 LE dimension
// (1=Nether, 2=End, omitted for Overworld) + one tag byte + (for
// SubChunkPrefix only) one subchunk-index byte. The four possible exact
// lengths (9, 10, 13, 14 bytes) disambiguate which fields are present --
// there's no other ambiguity to resolve.

enum class ChunkKeyTag : uint8_t {
    Data3D = 0x2B,
    Version = 0x2C,
    Data2D = 0x2D,
    Data2DLegacy = 0x2E,
    SubChunkPrefix = 0x2F,
    LegacyTerrain = 0x30,
    BlockEntity = 0x31,
    Entity = 0x32,
    PendingTicks = 0x33,
    LegacyBlockExtraData = 0x34,
    BiomeState = 0x35,
    FinalizedState = 0x36,
    ConversionData = 0x37,
    BorderBlocks = 0x38,
    HardcodedSpawners = 0x39,
    RandomTicks = 0x3A,
    Checksums = 0x3B,
    MetaDataHash = 0x3D,
    BlendingBiomeHeight = 0x3F,
    BlendingData = 0x40,
    ActorDigestVersion = 0x41,
    LegacyVersion = 0x76,
    AABBVolumes = 0x77,
    Unknown = 0xFF, // tag byte present in the key but not in the table above
};

struct ChunkKeyInfo {
    int32_t x = 0;
    int32_t z = 0;
    int32_t dimension = 0; // 0 = Overworld, 1 = Nether, 2 = End
    ChunkKeyTag tag = ChunkKeyTag::Unknown;
    optional<uint8_t> subChunkIndex; // only set when tag == SubChunkPrefix
};

// Parses one raw leveldb key. Returns nullopt if `key`/`keyLen` doesn't
// match one of the four valid chunk-key lengths (9/10/13/14 bytes) --
// that's expected and normal for the many non-chunk keys a real world's
// database also contains (~local_player, actorprefix*, map_*, village
// data, etc., per the Wiki's "Other keys" list) -- callers should treat
// those as "not a chunk key, skip" rather than an error.
optional<ChunkKeyInfo> parseChunkKey(const uint8_t* key, size_t keyLen);

// Human-readable name for a tag, for logging (e.g. "SubChunkPrefix",
// "Version", "Data3D"). Returns "Unknown(0xNN)" for tag bytes not in the
// table above.
string chunkKeyTagName(ChunkKeyTag tag);

// Opens <worldPath>/db read-only-ish (create_if_missing=0, so it will
// never create a database that isn't already there) with the same Zlib
// compressor real Bedrock worlds need (see WorldStorage.h), iterates
// every key, and logs a summary: total key count, how many parsed as
// chunk keys (broken down by tag), how many distinct chunk columns (x,z)
// were seen, and the coordinate range covered. Also decodes real block
// data (see RealSubChunk.h) for up to 3 sample SubChunkPrefix records so
// you can see actual block names, not just key shapes. Feeding decoded
// blocks into what a client is actually sent (converting names to
// network IDs, wiring into LevelChunkPacket.cpp) is still not done --
// see the project's roadmap. Safe to call on any world -- including
// LBCore's own freshly-generated ones, which will correctly report zero
// chunk keys found under this scheme, since LBCore's own chunks are
// stored under WorldStorage.cpp's separate "chunk:x:z" key scheme
// instead.
void scanAndReportWorldChunkKeys(const string& worldPath);
