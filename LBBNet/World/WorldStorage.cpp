#include "WorldStorage.h"
#include "LevelDat.h"
#include <leveldb/c.h>
#include <sys/stat.h>
#include <fstream>
#include <iostream>
#include <cstring>

using namespace std;

namespace {

leveldb_t* g_db = nullptr;
leveldb_readoptions_t* g_readOptions = nullptr;
leveldb_writeoptions_t* g_writeOptions = nullptr;

string chunkKey(int32_t chunkX, int32_t chunkZ) {
    // This project's own key scheme -- see the scope note in
    // WorldStorage.h for why this deliberately does NOT match Mojang's
    // real on-disk chunk key format.
    return "chunk:" + to_string(chunkX) + ":" + to_string(chunkZ);
}

bool directoryExists(const string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && (st.st_mode & S_IFDIR);
}

// Creates `path` AND any missing parent directories (i.e. "mkdir -p"),
// since a world now lives at "worlds/<levelName>" — a single mkdir()
// call fails if "worlds" itself doesn't exist yet, which it won't on a
// truly first run.
void makeDirectory(const string& path) {
    string partial;
    size_t pos = 0;

    while (pos <= path.size()) {
        size_t next = path.find('/', pos);
        if (next == string::npos) next = path.size();

        partial = path.substr(0, next);
        if (!partial.empty() && !directoryExists(partial)) {
            mkdir(partial.c_str(), 0755);
        }

        pos = next + 1;
    }
}

} // namespace

bool worldFolderExists(const string& worldPath) {
    ifstream f(worldPath + "/level.dat");
    return f.good();
}

void openOrCreateWorld(const string& worldPath, const string& levelName,
                        int32_t spawnX, int32_t spawnY, int32_t spawnZ) {
    bool alreadyExists = worldFolderExists(worldPath);

    if (!alreadyExists) {
        cout << "[WorldStorage] No world found at \"" << worldPath
             << "\" -- generating a fresh one\n";
        if (!directoryExists(worldPath)) makeDirectory(worldPath);
        writeLevelDat(worldPath, levelName, spawnX, spawnY, spawnZ);
    } else {
        cout << "[WorldStorage] Found existing world at \"" << worldPath
             << "\" -- loading it instead of generating\n";
        LevelDatInfo info = readLevelDat(worldPath);
        if (info.valid) {
            cout << "[WorldStorage]   LevelName: " << info.levelName
                 << ", Spawn: (" << info.spawnX << ", " << info.spawnY << ", "
                 << info.spawnZ << ")\n";
        } else {
            cout << "[WorldStorage]   level.dat present but couldn't be parsed -- "
                    "leaving it untouched, chunk storage will still be opened below\n";
        }
    }

    leveldb_options_t* options = leveldb_options_create();
    leveldb_options_set_create_if_missing(options, 1);
    // REQUIRED for opening real Bedrock worlds (see WorldStorage.h) --
    // if this identifier fails to compile, your installed leveldb/c.h is
    // either vanilla Google leveldb (wrong library entirely -- see
    // WorldStorage.h for which fork to build instead) or a fork that
    // spells this enum value differently; check the header directly.
    leveldb_options_set_compression(options, leveldb_zlib_raw_compression);

    char* err = nullptr;
    string dbPath = worldPath + "/db";
    g_db = leveldb_open(options, dbPath.c_str(), &err);
    leveldb_options_destroy(options);

    if (err != nullptr) {
        cout << "[WorldStorage] Failed to open chunk database at \"" << dbPath
             << "\": " << err << "\n";
        leveldb_free(err);
        g_db = nullptr;
        return;
    }

    g_readOptions = leveldb_readoptions_create();
    g_writeOptions = leveldb_writeoptions_create();
    cout << "[WorldStorage] Chunk database ready at \"" << dbPath << "\"\n";
}

bool tryLoadChunkRaw(int32_t chunkX, int32_t chunkZ, vector<uint8_t>& outPayload) {
    if (!g_db) return false;

    string key = chunkKey(chunkX, chunkZ);
    size_t valLen = 0;
    char* err = nullptr;
    char* value = leveldb_get(g_db, g_readOptions, key.data(), key.size(), &valLen, &err);

    if (err != nullptr) {
        cout << "[WorldStorage] Error reading chunk (" << chunkX << ", " << chunkZ
             << "): " << err << "\n";
        leveldb_free(err);
        if (value) leveldb_free(value);
        return false;
    }
    if (!value) return false; // key not present -- not an error, just not saved yet

    outPayload.assign(value, value + valLen);
    leveldb_free(value);
    return true;
}

void saveChunkRaw(int32_t chunkX, int32_t chunkZ, const vector<uint8_t>& payload) {
    if (!g_db) return;

    string key = chunkKey(chunkX, chunkZ);
    char* err = nullptr;
    leveldb_put(g_db, g_writeOptions, key.data(), key.size(),
                reinterpret_cast<const char*>(payload.data()), payload.size(), &err);

    if (err != nullptr) {
        cout << "[WorldStorage] Error saving chunk (" << chunkX << ", " << chunkZ
             << "): " << err << "\n";
        leveldb_free(err);
    }
}

void closeWorldStorage() {
    if (g_readOptions) { leveldb_readoptions_destroy(g_readOptions); g_readOptions = nullptr; }
    if (g_writeOptions) { leveldb_writeoptions_destroy(g_writeOptions); g_writeOptions = nullptr; }
    if (g_db) { leveldb_close(g_db); g_db = nullptr; }
}
