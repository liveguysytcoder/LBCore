#include "Multiplayer.h"
#include "DataTypeHelper.h"
#include "GamePacketSender.h"
#include "PlayerListPacket.h"
#include "TextPacket.h"
#include "LevelChunkPacket.h"
#include "NetworkChunkPublisherUpdatePacket.h"
#include "../../LBNet/Server/Server.h"
#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace std;

namespace {

// Position to show for a player: their last PlayerAuthInput position, or the
// world spawn until the first input arrives.
void playerPosition(const ClientState& s, float& x, float& y, float& z) {
    if (s.receivedAuthInput) { x = s.posX; y = s.posY; z = s.posZ; }
    else                     { x = 0.0f;   y = 4.0f;   z = 0.0f;   }
}

// Empty ItemInstance, byte-for-byte the same encoding InventoryContentPacket
// already uses for empty slots (proven against a real 26.51 client).
void writeEmptyItemInstance(vector<uint8_t>& p) {
    writeShort(p, 0);     // network id (air)
    writeUShort(p, 0);    // count
    writeVarInt(p, 0);    // metadata
    writeBool(p, false);  // has stack network id
    writeVarInt(p, 0);    // block runtime id
    vector<uint8_t> extra;
    writeUShort(extra, 0); // no NBT
    writeInt(extra, 0);    // can place on
    writeInt(extra, 0);    // can destroy
    writeVarInt(p, (uint32_t)extra.size());
    p.insert(p.end(), extra.begin(), extra.end());
}

// Same AbilityData UpdateAbilities sends (UpdateAbilitiesPacket.cpp).
void writeAbilityData(vector<uint8_t>& p, int64_t uniqueId) {
    writeInt64(p, uniqueId);
    writeUByte(p, 1);            // permission level: member
    writeUByte(p, 0);            // command permission: normal
    writeVarInt(p, 1);           // layers
    writeUShort(p, 1);           // base layer
    writeUInt(p, 0xFFFFF);       // abilities allowed
    writeUInt(p, 0xFFFFF);       // abilities enabled
    writeFloat(p, 0.05f);        // fly speed
    writeFloat(p, 0.05f);        // vertical fly speed
    writeFloat(p, 0.1f);         // walk speed
}

vector<uint8_t> buildAddPlayer(const ClientState& s) {
    vector<uint8_t> p;
    writeVarInt(p, 12); // AddPlayer
    writePlayerUuid(p, s.identityUUID);
    writeString(p, s.displayName);
    writeVarInt64(p, (uint64_t)s.entityId);     // runtime id
    writeString(p, "");                          // platform chat id
    float x, y, z; playerPosition(s, x, y, z);
    writeFloat(p, x); writeFloat(p, y); writeFloat(p, z);   // position
    writeFloat(p, 0.0f); writeFloat(p, 0.0f); writeFloat(p, 0.0f); // velocity
    writeFloat(p, s.pitch); writeFloat(p, s.yaw); writeFloat(p, s.headYaw);
    writeEmptyItemInstance(p);                   // held item
    writeZigZag32(p, 1);                         // game type (creative, as in the join sequence)
    writeVarInt(p, 0);                           // entity metadata: 0 entries
    writeVarInt(p, 0);                           // entity properties: 0 integer props
    writeVarInt(p, 0);                           // entity properties: 0 float props
    writeAbilityData(p, s.entityId);
    writeVarInt(p, 0);                           // entity links
    writeString(p, "");                          // device id
    writeInt(p, -1);                             // build platform (unknown)
    return p;
}

vector<uint8_t> buildMovePlayer(const ClientState& s) {
    vector<uint8_t> p;
    writeVarInt(p, 19); // MovePlayer
    writeVarInt64(p, (uint64_t)s.entityId);
    float x, y, z; playerPosition(s, x, y, z);
    writeFloat(p, x); writeFloat(p, y); writeFloat(p, z);
    writeFloat(p, s.pitch); writeFloat(p, s.yaw); writeFloat(p, s.headYaw);
    writeUByte(p, 0);                            // mode: normal
    writeBool(p, fabsf(s.deltaY) < 0.01f);       // on ground (approximation from vertical delta)
    writeVarInt64(p, 0);                         // ridden runtime id: none
    writeBool(p, false);                         // teleport data: absent
    writeVarInt64(p, s.lastAuthInputTick);       // tick
    return p;
}

void queueTranslatedMessage(ClientState& to, const char* key, const string& playerName) {
    vector<uint8_t> p;
    writeVarInt(p, 9);          // Text
    writeBool(p, true);         // needs translation
    writeUByte(p, 2);           // category: message with parameters
    writeUByte(p, 2);           // type: translation
    writeString(p, key);
    writeVarInt(p, 1);          // parameters
    writeString(p, playerName);
    writeString(p, "");         // xuid
    writeString(p, "");         // platform chat id
    writeBool(p, false);        // no filtered message
    queueGamePacket(to, p);
}

struct HookInstaller {
    HookInstaller() { g_onClientRemoved = &broadcastPlayerLeft; }
} g_hookInstaller;

} // namespace

void announcePlayerJoined(ClientState& joining) {
    joining.spawned = true;
    joining.lastChunkX = 0;
    joining.lastChunkZ = 0;

    int others = 0;
    forEachClient([&](ClientState& other) {
        if (&other == &joining || !other.spawned) return;
        others++;
        // Existing player -> joining player (queued; caller flushes the batch).
        sendPlayerListAddOf(joining, other, other.entityId);
        queueGamePacket(joining, buildAddPlayer(other));
        // Joining player -> existing player (sent now).
        sendPlayerListAddOf(other, joining, joining.entityId);
        queueGamePacket(other, buildAddPlayer(joining));
        sendJoinMessage(serverSocket, other.addr, other, joining.displayName);
        flushGamePackets(serverSocket, other.addr, other);
    });
    cout << "[Multiplayer] " << joining.displayName << " spawned (entity id "
         << joining.entityId << "), introduced to " << others << " other player(s)\n";
}

void broadcastPlayerLeft(ClientState& leaving) {
    vector<uint8_t> remove;
    writeVarInt(remove, 14); // RemoveActor
    writeZigZag64(remove, leaving.entityId);

    int notified = 0;
    forEachClient([&](ClientState& c) {
        if (&c == &leaving || !c.spawned) return;
        queueGamePacket(c, remove);
        sendPlayerListRemove(c, leaving);
        queueTranslatedMessage(c, "%multiplayer.player.left", leaving.displayName);
        flushGamePackets(serverSocket, c.addr, c);
        notified++;
    });
    cout << "[Multiplayer] " << leaving.displayName << " left, notified " << notified << " player(s)\n";
}

void broadcastMovement(ClientState& mover) {
    if (!mover.spawned) return;
    vector<uint8_t> pk = buildMovePlayer(mover);
    forEachClient([&](ClientState& c) {
        if (&c == &mover || !c.spawned) return;
        queueGamePacket(c, pk);
        flushGamePackets(serverSocket, c.addr, c);
    });
}

void streamChunksAround(int sock, sockaddr_in addr, ClientState& state) {
    if (!state.spawned || !state.receivedAuthInput) return;

    int32_t cx = (int32_t)floorf(state.posX / 16.0f);
    int32_t cz = (int32_t)floorf(state.posZ / 16.0f);
    const bool crossedChunk = (cx != state.lastChunkX || cz != state.lastChunkZ);

    // Re-send NetworkChunkPublisherUpdate periodically (every 60 ticks, ~3s
    // at 20 tps), not only when the player crosses a chunk boundary. A real
    // client is documented (and observed, e.g. the bedrock-protocol example
    // server) to use this packet as its cue to re-request any chunk within
    // radius it's still missing -- one send can be lost or arrive before
    // the client is ready to act on it, and there is otherwise no second
    // chance to recover from that during the initial join.
    const bool duePeriodic = state.lastAuthInputTick >= state.lastPublisherUpdateTick + 60;
    if (!crossedChunk && !duePeriodic) return;

    state.lastChunkX = cx;
    state.lastChunkZ = cz;
    state.lastPublisherUpdateTick = state.lastAuthInputTick;

    const int32_t R = kSentChunkRadius;
    sendNetworkChunkPublisherUpdate(sock, addr, state,
                                    (int32_t)floorf(state.posX),
                                    (int32_t)floorf(state.posY),
                                    (int32_t)floorf(state.posZ),
                                    R * 16);
    int sent = 0;
    for (int32_t dx = -R; dx <= R; dx++) {
        for (int32_t dz = -R; dz <= R; dz++) {
            if (!state.sentChunks.count({cx + dx, cz + dz})) {
                sendLevelChunk(sock, addr, state, cx + dx, cz + dz);
                sent++;
            }
        }
    }
    // Chunks that left the window are unloaded by the client; forget them so
    // they are sent again if the player walks back.
    for (auto it = state.sentChunks.begin(); it != state.sentChunks.end();) {
        if (abs(it->first - cx) > R || abs(it->second - cz) > R) it = state.sentChunks.erase(it);
        else ++it;
    }
    flushGamePackets(sock, addr, state);
    cout << "[Streaming] " << state.displayName << " entered chunk (" << cx << "," << cz
         << "): sent " << sent << " new chunk(s)\n";
}

void handleAnimate(ClientState& state, const vector<uint8_t>& data, size_t& offset) {
    uint8_t action = readUByte(data, offset);
    (void)readVarInt64(data, offset);           // sender's own runtime id (always 1 in its own view)
    float dataF = readFloat(data, offset);
    bool hasSwing = readBool(data, offset);
    string swing;
    if (hasSwing) swing = readString(data, offset);

    if (!state.spawned || action != 1) return;   // only arm swings are forwarded

    vector<uint8_t> p;
    writeVarInt(p, 44); // Animate
    writeUByte(p, action);
    writeVarInt64(p, (uint64_t)state.entityId);
    writeFloat(p, dataF);
    writeBool(p, hasSwing);
    if (hasSwing) writeString(p, swing);

    forEachClient([&](ClientState& c) {
        if (&c == &state || !c.spawned) return;
        queueGamePacket(c, p);
        flushGamePackets(serverSocket, c.addr, c);
    });
}
