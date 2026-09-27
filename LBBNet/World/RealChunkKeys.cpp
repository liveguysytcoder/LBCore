#include "RealChunkKeys.h"
#include "RealSubChunk.h"
#include <leveldb/c.h>
#include <iostream>
#include <map>
#include <set>
#include <cstring>

using namespace std;

namespace {

int32_t readI32LE(const uint8_t* p) {
    uint32_t u = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                 (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
    return static_cast<int32_t>(u);
}

} // namespace

optional<ChunkKeyInfo> parseChunkKey(const uint8_t* key, size_t keyLen) {
    if (keyLen != 9 && keyLen != 10 && keyLen != 13 && keyLen != 14) {
        return nullopt; // one of the many non-chunk keys real worlds also contain
    }

    ChunkKeyInfo info;
    info.x = readI32LE(key);
    info.z = readI32LE(key + 4);

    bool hasDimension = (keyLen == 13 || keyLen == 14);
    size_t tagOffset = hasDimension ? 12 : 8;

    info.dimension = hasDimension ? readI32LE(key + 8) : 0;
    info.tag = static_cast<ChunkKeyTag>(key[tagOffset]);

    bool hasSubChunkIndex = (keyLen == 10 || keyLen == 14);
    if (hasSubChunkIndex) {
        info.subChunkIndex = key[tagOffset + 1];
    }

    return info;
}

string chunkKeyTagName(ChunkKeyTag tag) {
    switch (tag) {
        case ChunkKeyTag::Data3D: return "Data3D";
        case ChunkKeyTag::Version: return "Version";
        case ChunkKeyTag::Data2D: return "Data2D";
        case ChunkKeyTag::Data2DLegacy: return "Data2DLegacy";
        case ChunkKeyTag::SubChunkPrefix: return "SubChunkPrefix";
        case ChunkKeyTag::LegacyTerrain: return "LegacyTerrain";
        case ChunkKeyTag::BlockEntity: return "BlockEntity";
        case ChunkKeyTag::Entity: return "Entity";
        case ChunkKeyTag::PendingTicks: return "PendingTicks";
        case ChunkKeyTag::LegacyBlockExtraData: return "LegacyBlockExtraData";
        case ChunkKeyTag::BiomeState: return "BiomeState";
        case ChunkKeyTag::FinalizedState: return "FinalizedState";
        case ChunkKeyTag::ConversionData: return "ConversionData";
        case ChunkKeyTag::BorderBlocks: return "BorderBlocks";
        case ChunkKeyTag::HardcodedSpawners: return "HardcodedSpawners";
        case ChunkKeyTag::RandomTicks: return "RandomTicks";
        case ChunkKeyTag::Checksums: return "Checksums";
        case ChunkKeyTag::MetaDataHash: return "MetaDataHash";
        case ChunkKeyTag::BlendingBiomeHeight: return "BlendingBiomeHeight";
        case ChunkKeyTag::BlendingData: return "BlendingData";
        case ChunkKeyTag::ActorDigestVersion: return "ActorDigestVersion";
        case ChunkKeyTag::LegacyVersion: return "LegacyVersion";
        case ChunkKeyTag::AABBVolumes: return "AABBVolumes";
        default: {
            char buf[32];
            snprintf(buf, sizeof(buf), "Unknown(0x%02X)", static_cast<unsigned>(tag));
            return string(buf);
        }
    }
}

void scanAndReportWorldChunkKeys(const string& worldPath) {
    leveldb_options_t* options = leveldb_options_create();
    leveldb_options_set_create_if_missing(options, 0); // never create -- diagnostic only
    leveldb_options_set_compression(options, leveldb_zlib_raw_compression);

    char* err = nullptr;
    string dbPath = worldPath + "/db";
    leveldb_t* db = leveldb_open(options, dbPath.c_str(), &err);
    leveldb_options_destroy(options);

    if (err != nullptr) {
        cout << "[RealChunkKeys] Could not open \"" << dbPath << "\" for scanning: "
             << err << "\n";
        leveldb_free(err);
        return;
    }

    leveldb_readoptions_t* readOptions = leveldb_readoptions_create();
    leveldb_iterator_t* it = leveldb_create_iterator(db, readOptions);

    size_t totalKeys = 0;
    size_t chunkKeys = 0;
    map<string, size_t> tagCounts;
    set<pair<int32_t, int32_t>> distinctColumns;
    int32_t minX = 0, maxX = 0, minZ = 0, maxZ = 0;
    bool haveBounds = false;

    // Stage 2 sample: decode real block data for a handful of
    // SubChunkPrefix records so the scan proves actual terrain is
    // readable, not just that keys parse. Capped at 3 samples -- this is
    // a startup diagnostic, not a full-world decode (that's Stage 4).
    constexpr int kMaxSamples = 3;
    int samplesDecoded = 0;

    for (leveldb_iter_seek_to_first(it); leveldb_iter_valid(it); leveldb_iter_next(it)) {
        totalKeys++;
        size_t keyLen = 0;
        const char* keyData = leveldb_iter_key(it, &keyLen);

        // LBCore's OWN key scheme (WorldStorage.cpp's "chunk:<x>:<z>",
        // ASCII text) can accidentally be the exact same byte length as a
        // real binary Mojang chunk key (9/10/13/14 bytes) purely by
        // coincidence -- e.g. "chunk:0:0" is 9 bytes, same as a real
        // no-dimension chunk key. Without this check, parseChunkKey()
        // would happily "parse" those ASCII bytes as if they were binary
        // coordinates, producing nonsense (huge x/z values that are just
        // the ASCII codes of "chun"/"k:0:" reinterpreted as an int32, and
        // a bogus tag from whatever digit character lands at the tag
        // offset). A real binary key can never legitimately start with
        // these literal ASCII bytes, so skip anything that does before
        // attempting to parse it as real chunk data.
        if (keyLen >= 6 && memcmp(keyData, "chunk:", 6) == 0) continue;

        auto parsed = parseChunkKey(reinterpret_cast<const uint8_t*>(keyData), keyLen);
        if (!parsed) continue;

        chunkKeys++;
        tagCounts[chunkKeyTagName(parsed->tag)]++;
        distinctColumns.insert({parsed->x, parsed->z});

        if (!haveBounds) {
            minX = maxX = parsed->x;
            minZ = maxZ = parsed->z;
            haveBounds = true;
        } else {
            if (parsed->x < minX) minX = parsed->x;
            if (parsed->x > maxX) maxX = parsed->x;
            if (parsed->z < minZ) minZ = parsed->z;
            if (parsed->z > maxZ) maxZ = parsed->z;
        }

        if (parsed->tag == ChunkKeyTag::SubChunkPrefix && samplesDecoded < kMaxSamples) {
            size_t valLen = 0;
            char* err2 = nullptr;
            char* value = leveldb_get(db, readOptions, keyData, keyLen, &valLen, &err2);
            if (err2 != nullptr) {
                leveldb_free(err2);
            } else if (value) {
                vector<uint8_t> rawValue(value, value + valLen);
                leveldb_free(value);

                auto decoded = decodeSubChunkPrefix(rawValue);
                samplesDecoded++;
                cout << "[RealChunkKeys]   Sample subchunk at column (" << parsed->x << ", "
                     << parsed->z << ")";
                if (parsed->subChunkIndex) cout << ", index " << (int)*parsed->subChunkIndex;
                cout << ": ";
                if (!decoded) {
                    cout << "couldn't decode (unsupported/older subchunk version, or "
                            "malformed data)\n";
                } else {
                    map<string, int> nameCounts;
                    for (const string& name : decoded->blockNames) nameCounts[name]++;
                    cout << decoded->blockNames.size() << " block positions decoded, "
                         << nameCounts.size() << " distinct block types:\n";
                    for (const auto& [name, count] : nameCounts) {
                        cout << "[RealChunkKeys]     " << name << ": " << count << "\n";
                    }
                }
            }
        }
    }

    leveldb_iter_destroy(it);
    leveldb_readoptions_destroy(readOptions);
    leveldb_close(db);

    cout << "[RealChunkKeys] Scanned \"" << dbPath << "\":\n";
    cout << "[RealChunkKeys]   " << totalKeys << " total keys, " << chunkKeys
         << " parsed as chunk keys (the rest are entities/villages/maps/etc -- normal)\n";
    if (chunkKeys == 0) {
        cout << "[RealChunkKeys]   No chunk keys found. Either this is a brand new/empty "
                "world, or (if you expected real terrain here) something about this "
                "world's format doesn't match what parseChunkKey() expects -- worth "
                "double-checking the world actually has generated terrain near spawn.\n";
        return;
    }
    cout << "[RealChunkKeys]   " << distinctColumns.size() << " distinct chunk columns, "
         << "x range [" << minX << ", " << maxX << "], z range [" << minZ << ", " << maxZ << "]\n";
    for (const auto& [name, count] : tagCounts) {
        cout << "[RealChunkKeys]     " << name << ": " << count << "\n";
    }
    cout << "[RealChunkKeys]   Stage 1 confirmed: real chunk data is present and its keys "
            "parse correctly. Sample SubChunkPrefix decodes above (Stage 2) show whether "
            "real block data is readable too. Feeding decoded blocks into what's actually "
            "sent to a client (converting names to network IDs, wiring into "
            "LevelChunkPacket.cpp) is Stage 3/4, still not done.\n";
}
