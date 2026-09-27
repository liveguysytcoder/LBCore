#include <iostream>
#include "LBNet/Server/ServerManager.h"
#include "LBBNet/Server/ServerManager.h"
#include "LBBNet/Packets/Encryption.h"
#include "LBBNet/World/WorldStorage.h"
#include "LBBNet/World/RealChunkKeys.h"
#include "Logging/Logger.h"
#include "Config/Config.h"

int main() {
    // Must run before anything else prints — it takes over std::cout so
    // every line from here on is also written to lbcore.log.txt (which is
    // truncated first, so it only ever holds this run's output).
    initLogger();

    // Reads server.properties (port, bind-address, motd, etc) if present
    // next to the executable; falls back to built-in defaults otherwise.
    // Must run before LBStart(), since Server.cpp reads the port/bind
    // address from this at bind() time.
    loadConfig();

    // Needed before the first client can log in — real (Xbox Live signed
    // in, online) clients go through the encryption handshake right after
    // Login, which needs this key pair. See LBBNet/Packets/Encryption.h.
    generateServerKeyPair();

    // Every world lives under a top-level "worlds/" folder, named after
    // level-name from server.properties (default "World") — e.g.
    // "worlds/World", or "worlds/MySurvivalMap" if you changed it. Same
    // convention as PocketMine and most other server software, instead
    // of dumping a world folder directly next to the executable.
    const string worldPath = "worlds/" + getConfig().levelName;

    // Checked BEFORE openOrCreateWorld touches anything, so this reflects
    // whether a world was already sitting here (e.g. a real/downloaded
    // vanilla map you dropped in) as opposed to one LBCore itself
    // generated on a previous run.
    bool worldAlreadyExisted = worldFolderExists(worldPath);

    // Stage 1 diagnostic (see RealChunkKeys.h): only meaningful for a
    // world that already existed before this run -- LBCore's own freshly
    // generated worlds correctly have zero real chunk keys (they use
    // WorldStorage.cpp's own separate key scheme instead), so scanning a
    // brand new one would just be log noise. Drop a real/downloaded
    // vanilla world folder in as worlds/<levelName>/ and check
    // lbcore.log.txt after startup to see what this finds.
    //
    // MUST run BEFORE openOrCreateWorld() below, not after: LevelDB only
    // allows one open handle on a given database directory at a time
    // (it takes an exclusive lock file). openOrCreateWorld() opens and
    // holds its handle for the server's entire runtime, so a second,
    // independent open here -- if it ran afterward -- would always fail
    // with "already held by process". This diagnostic is read-only and
    // opens/closes its own short-lived handle, so it only works while
    // nothing else has the directory open yet.
    if (worldAlreadyExisted) {
        scanAndReportWorldChunkKeys(worldPath);
    }

    // Generates a fresh world (level.dat + leveldb chunk database) ONLY
    // if worlds/<levelName>/ doesn't already exist; otherwise opens what's
    // already there instead of touching it. See LBBNet/World/WorldStorage.h
    // for exactly what "opens what's already there" does and doesn't cover
    // for a real/downloaded vanilla map. makeDirectory() creates "worlds/"
    // itself too if this is a genuinely first run. Must run before
    // LBBStart(), since chunk sends (LevelChunkPacket.cpp) read/write
    // through this from the moment the first client spawns. This opens
    // and HOLDS the leveldb handle for the rest of the process's life --
    // must run after the diagnostic scan above, never before.
    openOrCreateWorld(worldPath, getConfig().levelName,
                       /*spawnX=*/0, /*spawnY=*/4, /*spawnZ=*/0);

    LBBStart();
    LBStart();
    return 0;
}