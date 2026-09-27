#include "UpdateAttributesPacket.h"
#include "DataTypeHelper.h"
#include "GamePacketSender.h"
#include <climits>
#include <cfloat>
#include <iostream>

using namespace std;

namespace {

// Writes a single Attribute entry. Field order confirmed directly
// against gophertunnel's Attribute.Marshal (minecraft/protocol/attribute.go):
// Min, Max, Value, DefaultMin, DefaultMax, Default, Name, Modifiers.
void writeAttribute(vector<uint8_t>& packet, const string& name,
                     float min, float max, float current,
                     float defaultMin, float defaultMax, float defaultValue) {
    writeFloat(packet, min);
    writeFloat(packet, max);
    writeFloat(packet, current);

    writeFloat(packet, defaultMin);
    writeFloat(packet, defaultMax);
    writeFloat(packet, defaultValue);

    writeString(packet, name); // Name (hashed_string -- same wire format as string)

    writeVarInt(packet, 0); // Modifiers: array count = 0
}

// Shared header + footer for every UpdateAttributes send: gamepacket ID,
// Runtime Entity ID, the Attributes array (built by the caller), and the
// trailing Tick field.
void sendAttributes(int sock, sockaddr_in clientAddr, ClientState& state,
                     const vector<function<void(vector<uint8_t>&)>>& attrWriters) {
    vector<uint8_t> packet;
    writeVarInt(packet, 29); // UpdateAttributes

    writeVarInt64(packet, 1); // Runtime Entity ID (this project's one player)

    writeVarInt(packet, (uint32_t)attrWriters.size()); // Attributes: array count
    for (auto& w : attrWriters) w(packet);

    writeVarInt64(packet, 0); // Tick -- placeholder until real tick tracking exists

    queueGamePacket(state, packet);
}

} // namespace

// Values copied directly from Dragonfly's Session.SendSpeed
// (server/session/player.go): minecraft:movement, min 0, max
// FLT_MAX, default 0.1.
void sendUpdateAttributesSpeed(int sock, sockaddr_in clientAddr, ClientState& state,
                                float speed) {
    sendAttributes(sock, clientAddr, state, {
        [&](vector<uint8_t>& p) {
            writeAttribute(p, "minecraft:movement",
                            0.0f, FLT_MAX, speed,
                            0.0f, FLT_MAX, 0.1f);
        }
    });
    cout << "[Bedrock] Sent UpdateAttributes(speed=" << speed << ")\n";
}

// Values copied directly from Dragonfly's Session.SendHealth: health
// (min 0, max/default-max = maxHealth, default 20) + absorption
// (min 0, max FLT_MAX, no default).
void sendUpdateAttributesHealth(int sock, sockaddr_in clientAddr, ClientState& state,
                                 float health, float maxHealth, float absorption) {
    sendAttributes(sock, clientAddr, state, {
        [&](vector<uint8_t>& p) {
            writeAttribute(p, "minecraft:health",
                            0.0f, maxHealth, health,
                            0.0f, 20.0f, 20.0f);
        },
        [&](vector<uint8_t>& p) {
            writeAttribute(p, "minecraft:absorption",
                            0.0f, FLT_MAX, absorption,
                            0.0f, FLT_MAX, 0.0f);
        }
    });
    cout << "[Bedrock] Sent UpdateAttributes(health=" << health << "/" << maxHealth
         << ", absorption=" << absorption << ")\n";
}

// Values copied directly from Dragonfly's Session.SendExperience:
// minecraft:player.level (min 0, max INT32_MAX) + minecraft:player.experience
// (progress, min 0, max 1).
void sendUpdateAttributesExperience(int sock, sockaddr_in clientAddr, ClientState& state,
                                     int32_t level, float progress) {
    sendAttributes(sock, clientAddr, state, {
        [&](vector<uint8_t>& p) {
            writeAttribute(p, "minecraft:player.level",
                            0.0f, (float)INT32_MAX, (float)level,
                            0.0f, (float)INT32_MAX, 0.0f);
        },
        [&](vector<uint8_t>& p) {
            writeAttribute(p, "minecraft:player.experience",
                            0.0f, 1.0f, progress,
                            0.0f, 1.0f, 0.0f);
        }
    });
    cout << "[Bedrock] Sent UpdateAttributes(level=" << level << ", progress=" << progress << ")\n";
}

// Values copied directly from Dragonfly's Session.SendFood: hunger
// (min 0, max/default-max/default 20) + saturation (same bounds) +
// exhaustion (min 0, max/default-max 5, no default).
void sendUpdateAttributesFood(int sock, sockaddr_in clientAddr, ClientState& state,
                               int32_t food, float saturation, float exhaustion) {
    sendAttributes(sock, clientAddr, state, {
        [&](vector<uint8_t>& p) {
            writeAttribute(p, "minecraft:player.hunger",
                            0.0f, 20.0f, (float)food,
                            0.0f, 20.0f, 20.0f);
        },
        [&](vector<uint8_t>& p) {
            writeAttribute(p, "minecraft:player.saturation",
                            0.0f, 20.0f, saturation,
                            0.0f, 20.0f, 20.0f);
        },
        [&](vector<uint8_t>& p) {
            writeAttribute(p, "minecraft:player.exhaustion",
                            0.0f, 5.0f, exhaustion,
                            0.0f, 5.0f, 0.0f);
        }
    });
    cout << "[Bedrock] Sent UpdateAttributes(food=" << food << ", saturation=" << saturation
         << ", exhaustion=" << exhaustion << ")\n";
}
