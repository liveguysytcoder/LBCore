#pragma once
#include <cstdint>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "../../LBNet/Server/Clients.h"

using namespace std;

// Sends UpdateAttributes (29 / 0x1D — confirmed directly against
// gophertunnel's own packet/id.go const block by counting its iota
// sequence, including the two unnamed `_` reserved slots at 20 and
// 23/24 that a naive sequential count would miss).
//
// FIELD LAYOUT CONFIRMED: gophertunnel's real Attribute.Marshal (in
// minecraft/protocol/attribute.go) was read directly this session —
// Min, Max, Value(Current), DefaultMin, DefaultMax, Default, Name,
// Modifiers — which matches exactly what this project already had.
// The earlier uncertainty note (this file used to say the order
// couldn't be pulled from source) is resolved: no change needed there.
//
// SPLIT INTO 4 FUNCTIONS: a real Dragonfly capture showed
// UpdateAttributes sent 4 times during spawn, with PlayerList
// interleaved after the first. Reading Dragonfly's own
// server/session/player.go confirms why: these are 4 DIFFERENT calls
// (SendSpeed, then later SendHealth/SendExperience/SendFood from
// Session.Spawn(), in that exact order), not the same health packet
// repeated. Values, mins/maxes and defaults below are copied directly
// from those functions.
void sendUpdateAttributesSpeed(int sock, sockaddr_in clientAddr, ClientState& state,
                                float speed);

void sendUpdateAttributesHealth(int sock, sockaddr_in clientAddr, ClientState& state,
                                 float health, float maxHealth, float absorption);

void sendUpdateAttributesExperience(int sock, sockaddr_in clientAddr, ClientState& state,
                                     int32_t level, float progress);

void sendUpdateAttributesFood(int sock, sockaddr_in clientAddr, ClientState& state,
                               int32_t food, float saturation, float exhaustion);
