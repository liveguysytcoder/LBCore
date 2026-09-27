#include "LevelDat.h"
#include "NbtClassic.h"
#include <fstream>
#include <vector>
#include <iostream>

using namespace std;

namespace {

constexpr int32_t kLevelDatStorageVersion = 10; // per Minecraft Wiki: "currently 10"

void writeI32LE(vector<uint8_t>& buf, int32_t value) {
    uint32_t u = static_cast<uint32_t>(value);
    buf.push_back(u & 0xFF);
    buf.push_back((u >> 8) & 0xFF);
    buf.push_back((u >> 16) & 0xFF);
    buf.push_back((u >> 24) & 0xFF);
}

} // namespace

LevelDatInfo readLevelDat(const string& worldPath) {
    LevelDatInfo info;
    ifstream file(worldPath + "/level.dat", ios::binary);
    if (!file) return info; // not present -- not an error, just "no world here yet"

    vector<uint8_t> raw((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
    if (raw.size() < 8) return info; // too short to even have the header

    // Header: int32 LE version, int32 LE body length. We don't reject on
    // an unexpected version -- older/newer real worlds may use a
    // different value here and the NBT body itself is still classic-LE
    // either way -- we just use the declared length as a bounds check.
    uint32_t declaredLen = static_cast<uint32_t>(raw[4]) |
                           (static_cast<uint32_t>(raw[5]) << 8) |
                           (static_cast<uint32_t>(raw[6]) << 16) |
                           (static_cast<uint32_t>(raw[7]) << 24);
    if (8 + (size_t)declaredLen > raw.size()) {
        cout << "[WorldStorage] level.dat declared length exceeds actual file size -- "
                "treating as unreadable\n";
        return info;
    }

    size_t offset = 8;
    NbtCompoundView view;
    if (!readNbtCompoundClassic(raw, offset, view)) {
        cout << "[WorldStorage] level.dat NBT body couldn't be parsed -- treating as "
                "unreadable (won't overwrite; see WorldStorage.h for what happens next)\n";
        return info;
    }

    info.valid = true;
    if (view.strings.count("LevelName")) info.levelName = view.strings["LevelName"];
    if (view.ints.count("SpawnX")) info.spawnX = view.ints["SpawnX"];
    if (view.ints.count("SpawnY")) info.spawnY = view.ints["SpawnY"];
    if (view.ints.count("SpawnZ")) info.spawnZ = view.ints["SpawnZ"];
    return info;
}

void writeLevelDat(const string& worldPath, const string& levelName,
                    int32_t spawnX, int32_t spawnY, int32_t spawnZ) {
    vector<uint8_t> body;
    writeNbtCompoundStartClassic(body);
    writeNbtStringClassic(body, "LevelName", levelName);
    writeNbtIntClassic(body, "SpawnX", spawnX);
    writeNbtIntClassic(body, "SpawnY", spawnY);
    writeNbtIntClassic(body, "SpawnZ", spawnZ);
    writeNbtIntClassic(body, "StorageVersion", kLevelDatStorageVersion);
    writeNbtIntClassic(body, "NetworkVersion", 2193); // see server.properties' protocol-version
    writeNbtLongClassic(body, "Time", 0);
    writeNbtLongClassic(body, "currentTick", 0);
    writeNbtCompoundEndClassic(body);

    vector<uint8_t> out;
    writeI32LE(out, kLevelDatStorageVersion);
    writeI32LE(out, static_cast<int32_t>(body.size()));
    out.insert(out.end(), body.begin(), body.end());

    ofstream file(worldPath + "/level.dat", ios::binary | ios::trunc);
    if (!file) {
        cout << "[WorldStorage] Could not open " << worldPath << "/level.dat for writing\n";
        return;
    }
    file.write(reinterpret_cast<const char*>(out.data()), out.size());
    cout << "[WorldStorage] Wrote fresh level.dat (" << out.size() << " bytes) at "
         << worldPath << "\n";
}
