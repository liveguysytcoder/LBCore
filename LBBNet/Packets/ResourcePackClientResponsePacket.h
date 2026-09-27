#pragma once
#include <vector>
#include <cstdint>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "../../LBNet/Server/Clients.h"

using namespace std;

// Status values for ResourcePackClientResponse's leading byte, per the
// Bedrock Protocol wiki's ResourcePackClientResponse packet.
enum class ResourcePackResponseStatus : uint8_t {
    Refused      = 0,
    SendPacks    = 1,
    HaveAllPacks = 2,
    Completed    = 3,
};

// Parses ResourcePackClientResponse (0x08) — offset must already point past
// the varint gamepacket header, i.e. at the start of the status byte — and
// drives the next step of the resource pack stage: replying with
// ResourcePacksStack once the client confirms it has everything it needs,
// since we never offer any packs of our own.
void handleResourcePackClientResponse(int sock, sockaddr_in clientAddr, ClientState& state,
                                       const vector<uint8_t>& data, size_t offset);

// Sends StartGame (plus the definition packets that follow it elsewhere in
// this call chain) once the resource pack stage is done. Guarded by
// state.startGameSent so it's safe to call this proactively right after
// ResourcePacksStack (see ResourcePackStackPacket.cpp) as well as from a
// real Completed response, without sending it twice. Does NOT send chunks,
// inventory, or PlayStatus(PlayerSpawn) — see sendSpawnSequence() below.
void completeResourcePackStageAndStartGame(int sock, sockaddr_in clientAddr, ClientState& state);

// Sends the rest of the spawn sequence (chunks around spawn, inventory
// content, PlayStatus(PlayerSpawn)) — split out from
// completeResourcePackStageAndStartGame() because a real client needs to
// drive this step itself by sending RequestChunkRadius first. Call this
// from PacketHandler.cpp's RequestChunkRadius (0x45) case, after replying
// with ChunkRadiusUpdated. Guarded by state.spawnSequenceSent so a client
// that sends RequestChunkRadius more than once doesn't get it all resent.
void sendSpawnSequence(int sock, sockaddr_in clientAddr, ClientState& state);

// Second half of the spawn sequence: biomes, attributes, player list, world
// state, inventories, commands and the initial chunks. Runs at most once per
// client. `trigger` is only used for logging (which event released it).
void sendSpawnSequencePhase2(int sock, sockaddr_in clientAddr, ClientState& state, const char* trigger);
