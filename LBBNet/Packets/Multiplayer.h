#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "../../LBNet/Server/Clients.h"

using namespace std;

// ---------------------------------------------------------------------------
// Multiplayer support: making players visible to each other, streaming
// chunks as a player moves, and cleaning up when someone leaves.
//
// Wire layouts (verified against gophertunnel v1.62.0 = protocol 2193):
//   AddPlayer(12)   UUID, username, runtimeId(varuint64), platformChatId,
//                   position(vec3), velocity(vec3), pitch, yaw, headYaw,
//                   heldItem(ItemInstance), gameType(varint32), metadata,
//                   entityProperties(2 slices), abilityData, links(slice),
//                   deviceId, buildPlatform(int32)
//   MovePlayer(19)  runtimeId(varuint64), position, pitch, yaw, headYaw,
//                   mode(u8), onGround, riddenRuntimeId(varuint64),
//                   teleportData(optional: bool [+data]), tick(varuint64)
//   RemoveActor(14) uniqueId(varint64)
//   Animate(44)     action(u8), runtimeId(varuint64), data(f32),
//                   swingSource(optional string: bool [+string])
// ---------------------------------------------------------------------------

// Called from sendSpawnSequence() just before the joining client's batch is
// flushed. Queues the already-connected players onto the joining client's
// batch, and immediately tells every existing player about the new one.
void announcePlayerJoined(ClientState& joining);

// Tells every other player that `leaving` is gone (also installed as the
// removeClient() hook, so it runs on any disconnect/timeout).
void broadcastPlayerLeft(ClientState& leaving);

// Sends `mover`'s current position/rotation to every other spawned player.
void broadcastMovement(ClientState& mover);

// If the player crossed into a new chunk: re-centre the client's chunk
// publisher and send the chunks that just came into range.
void streamChunksAround(int sock, sockaddr_in addr, ClientState& state);

// Inbound Animate (arm swing): parsed and forwarded to the other players.
void handleAnimate(ClientState& state, const vector<uint8_t>& data, size_t& offset);
