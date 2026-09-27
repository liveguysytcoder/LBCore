#include "UpdateAbilitiesPacket.h"
#include "DataTypeHelper.h"
#include "GamePacketSender.h"
#include <iostream>

using namespace std;

void sendUpdateAbilities(int sock, sockaddr_in clientAddr, ClientState& state) {
    vector<uint8_t> packet;
    writeVarInt(packet, 187); // UpdateAbilities (0xBB) -- per PocketMine-MP's
                               // current ProtocolInfo.php.

    // FIXED (confirmed against real protocol.json for 1.26.40): entity_unique_id
    // is a raw little-endian signed 64-bit int (li64), NOT a zigzag varint.
    // Using writeZigZag64 here desynced every byte that followed it in the
    // packet -- this was the real root cause behind the downstream li32/lf32
    // read failures, not AbilityLayers itself.
    writeInt64(packet, 1);    // entity_unique_id: li64
    writeUByte(packet, 1);    // permission_level: member (matches reference's "member")
    writeUByte(packet, 0);    // command_permission: normal (matches reference's "normal")

    writeUByte(packet, 1);       // abilities: array count = 1
    // FIXED: layer type is a lu16 mapper where 0 = "cache" and 1 = "base" --
    // we want "base", so this must be 1, not 0.
    writeUShort(packet, 1);      // AbilityLayers.type = base (1)
    writeUInt(packet, 0xFFFFF);  // allowed: AbilitySet (lu32 bitflags) -- reference
                                  // capture's combined raw value for a creative
                                  // player (all 20 real ability bits set true)
    writeUInt(packet, 0xFFFFF);  // enabled: AbilitySet -- duplicate of allowed
    writeFloat(packet, 0.05f);   // fly_speed: lf32 (vanilla default)
    // FIXED: vertical_fly_speed (lf32) was missing entirely -- the real
    // schema has it between fly_speed and walk_speed, so the packet was one
    // float short and everything from walk_speed onward was being misread.
    writeFloat(packet, 0.05f);   // vertical_fly_speed: lf32
    writeFloat(packet, 0.1f);    // walk_speed: lf32 (vanilla default)

    queueGamePacket(state, packet);
    cout << "[Bedrock] Sent UpdateAbilities\n";
}
