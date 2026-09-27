#pragma once
#include <cstdint>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "../../LBNet/Server/Clients.h"

using namespace std;

// Sends PlayerList (63 / 0x3F), type="add", registering the connecting
// player's own identity + skin. Confirmed via a real Dragonfly capture
// that a working server sends exactly one of these (type add) right
// after UpdateAttributes -- this project never sent one at all.
//
// Real client symptom this targets: after spawning, the client gets
// stuck on a "loading splitscreen appearance" screen and then disconnects
// with a generic InitialConnection-90 error. That screen is specifically
// about the client preparing player skin/appearance data -- consistent
// with the client waiting on a PlayerList entry (with skin data) that
// never arrives, then timing out.
//
// Every field here is confirmed against the real protocol.json for 1.26.40
// (packet_player_list -> PlayerRecords -> PlayerRecord -> Skin -> SkinImage),
// cross-checked against protodef's actual numeric type implementations
// (numeric.js) and bedrock-protocol's native uuid read/write (minecraft.js),
// not just the field-name conventions. That deeper check caught two real
// bugs this file previously had: the uuid field was being byte-reversed in
// two 8-byte halves instead of copied as 16 raw bytes, and skin_color /
// player_color were written little-endian when their declared type (i32,
// no "l" prefix) is actually big-endian in this protocol. Nothing here is
// guessed.
//
// Skin data: uses the real skin the connecting client sent in its own
// Login packet client-data JWT (see extractClientSkinData() in
// LoginPacket.h/.cpp -- field names verified against minecraft-data's
// steve.json, the same ClientData shape bedrock-protocol's own client
// login sends) whenever state.hasSkinData is true. Falls back to a
// minimal spec-valid placeholder (a 64x64 flat-color texture -- 64x64 is
// one of Minecraft's fixed valid skin preset sizes, not an arbitrary
// width*height*4-consistent size, which is what this file's original
// 1x1 placeholder got wrong and caused the spawn hang -- plus a 0x0
// "no cape" and the built-in default humanoid geometry) only for
// connections that sent no usable clientData.
void sendPlayerListAdd(int sock, sockaddr_in clientAddr, ClientState& state);

// Multiplayer variants (see Multiplayer.cpp).
void sendPlayerListAddOf(ClientState& receiver, ClientState& subject, int64_t entityUniqueId);
void sendPlayerListRemove(ClientState& receiver, ClientState& subject);
void writePlayerUuid(vector<uint8_t>& buf, const string& uuidStr);
