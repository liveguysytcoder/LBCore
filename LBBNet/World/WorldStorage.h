#pragma once
#include <vector>
#include <string>
#include <cstdint>

using namespace std;

// Real LevelDB-backed persistence for LBCore's world data, using the
// leveldb C API (leveldb/c.h).
//
// IMPORTANT -- read before building: this MUST be linked against
// Mojang's leveldb-mcpe fork (https://github.com/Mojang/leveldb-mcpe), or
// a maintained fork of it (e.g. hallipr/leveldb-mcpe-cs,
// purple-blog/build-bedrock-leveldb), NOT vanilla Google leveldb.
// Real Bedrock world saves are compressed with a custom Zlib compressor
// Mojang added to their fork; standard leveldb only knows Snappy/none and
// cannot open real Bedrock db/ folders at all. This project sets
// leveldb_zlib_raw_compression below specifically because it's required
// for opening real worlds, even though (see the scope note further down)
// this project doesn't yet read their actual chunk contents.
//
// SCOPE OF WHAT THIS ACTUALLY DOES RIGHT NOW -- read this before assuming
// a downloaded map "just works":
//   This project uses its OWN key scheme ("chunk:<x>:<z>", holding this
//   project's own raw LevelChunk packet payload bytes -- see
//   LevelChunkPacket.cpp) inside the leveldb database, NOT Mojang's real
//   on-disk key format (which encodes x/z/dimension/tag as packed binary,
//   per-subchunk, with real block-palette NBT as the value). That means:
//     - LBCore's OWN generated world now genuinely persists across
//       restarts (this works today).
//     - Opening a real/downloaded vanilla world folder will NOT crash --
//       level.dat is read (see LevelDat.h), and the leveldb database
//       opens fine with the right compressor -- but a lookup for
//       "chunk:0:0" will find nothing under Mojang's real key scheme, so
//       LBCore will just fall back to generating (and then saving, under
//       ITS OWN keys, alongside the untouched real data) its own
//       procedural terrain instead. The real terrain is not deleted or
//       corrupted, but it is not rendered either.
//   Actually loading and rendering a real map's real terrain needs a
//   follow-up project: parsing Mojang's real chunk keys/subchunk NBT, and
//   critically, converting arbitrary real block-palette entries into
//   network IDs LBCore's sender understands (LevelChunkPacket.cpp
//   currently only knows bedrock/dirt/grass/air via BlockHash.cpp's
//   hash table) -- a real downloaded map will reference many more block
//   types than that.

// True if <worldPath>/level.dat already exists -- the single source of
// truth for "generate a fresh world" vs "load what's already here".
// Per the requirement this was built for: generation should only ever
// happen when this returns false.
bool worldFolderExists(const string& worldPath);

// Creates <worldPath> (and a fresh level.dat + empty leveldb database
// inside it) if it doesn't exist yet; opens the existing database
// otherwise. Must be called once at startup before any chunk load/save
// call below. Safe to call even if worldFolderExists() is already true --
// it will open, not overwrite, in that case.
void openOrCreateWorld(const string& worldPath, const string& levelName,
                        int32_t spawnX, int32_t spawnY, int32_t spawnZ);

// Looks up this project's own raw LevelChunk payload for (chunkX, chunkZ)
// under this project's own key scheme (see the scope note above -- this
// will NOT find real vanilla chunk data). Returns false if not present
// (either a brand new world, or a real map whose real keys don't match
// ours) -- callers should fall back to procedural generation in that case.
bool tryLoadChunkRaw(int32_t chunkX, int32_t chunkZ, vector<uint8_t>& outPayload);

// Saves `payload` (this project's own raw LevelChunk payload bytes) under
// this project's own key for (chunkX, chunkZ), so it's there to load on
// the next restart instead of being regenerated identically every time.
void saveChunkRaw(int32_t chunkX, int32_t chunkZ, const vector<uint8_t>& payload);

// Flushes and closes the leveldb handle. Not currently called anywhere
// (this project has no graceful-shutdown path yet), but provided for
// when one exists -- leveldb handles system file handles that should be
// released cleanly rather than left to process-exit cleanup.
void closeWorldStorage();
