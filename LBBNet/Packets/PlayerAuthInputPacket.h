#pragma once
#include <vector>
#include <cstdint>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "../../LBNet/Server/Clients.h"

using namespace std;

// PlayerAuthInput (id 144). Parsed layout, verified against gophertunnel
// v1.62.0 (protocol 2193):
//   pitch, yaw (f32), position (vec3), moveVector (vec2), headYaw (f32),
//   inputFlags (varuint32 count + varint32 flag ids), inputMode (varuint32),
//   playMode (varuint32), interactionModel (varint32), interactPitch,
//   interactYaw (f32), tick (varuint64), delta (vec3), then a chain of
//   optionals (item interaction, item stack request, block actions, vehicle
//   rotation, predicted vehicle) followed by analogue move vector, camera
//   orientation and raw move vector.
// Everything through `delta` is parsed and stored in ClientState. The
// optional chain is only inspected as far as it can be walked safely.
// After parsing, the player's movement is broadcast to other players and
// new chunks are streamed if the player entered a new chunk.
void handlePlayerAuthInput(int sock, sockaddr_in clientAddr, ClientState& state,
                           const vector<uint8_t>& data, size_t& offset);
