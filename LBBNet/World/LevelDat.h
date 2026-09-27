#pragma once
#include <string>
#include <cstdint>

using namespace std;

// Bedrock's level.dat: per Minecraft Wiki's Bedrock Edition level format
// page, the file is an 8-byte header -- a little-endian int32 "storage
// version" (currently 10) followed by a little-endian int32 "length of
// the rest of the file" -- then a classic-LE NBT compound (see
// NbtClassic.h) body of exactly that length. Distinct from level.dat_old
// (a backup copy some game versions write) and from the db/ folder
// (chunk/entity data — see WorldStorage.h), which is a separate LevelDB
// database sitting next to this file, not inside it.

struct LevelDatInfo {
    bool valid = false;
    string levelName;
    int32_t spawnX = 0;
    int32_t spawnY = 64;
    int32_t spawnZ = 0;
};

// Reads <worldPath>/level.dat if present. Returns valid=false (not an
// error the caller needs to act on) if the file is missing, truncated, or
// doesn't start with a recognizable Compound tag -- callers should treat
// that the same as "no usable level.dat" and fall back to generating a
// fresh one via writeLevelDat().
LevelDatInfo readLevelDat(const string& worldPath);

// Writes a fresh, minimal-but-valid <worldPath>/level.dat. Only fills in
// the handful of fields this project actually reads back or that a real
// client's/tool's basic sanity checks might look for (LevelName,
// SpawnX/Y/Z, StorageVersion, NetworkVersion) -- NOT a byte-exact replica
// of everything a real vanilla-generated level.dat contains (hundreds of
// gamerule/experiment fields this project doesn't use). Good enough for
// this server to recognize its own world on the next restart; not
// guaranteed to satisfy every third-party tool that inspects level.dat.
void writeLevelDat(const string& worldPath, const string& levelName,
                    int32_t spawnX, int32_t spawnY, int32_t spawnZ);
