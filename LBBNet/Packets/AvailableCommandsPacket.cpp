#include "AvailableCommandsPacket.h"
#include "DataTypeHelper.h"
#include "GamePacketSender.h"
#include "Chat.h"
#include "../../Config/Config.h"
#include <iostream>

using namespace std;

// AvailableCommands (id 76). Layout verified against gophertunnel v1.62.0
// (protocol 2193), AvailableCommands.Marshal(): EIGHT slices in this order --
//   enumValues, chainedSubcommandValues, suffixes, enums, chainedSubcommands,
//   commands, dynamicEnums, constraints
// each with a varuint count. (The previous version wrote nine zero counts;
// the client tolerated the extra byte, but eight is the real layout.)
//
// Command entry: name, description, flags(u16), permission(STRING in 2193),
// aliasesOffset(u32, 0xFFFFFFFF = none), chainedSubcommandOffsets(slice of
// u32), overloads(slice of {chaining bool, parameters slice}).
// The built-in commands take no arguments, so each has one empty overload.
//
// With enable-commands=false in server.properties the original empty
// command list is sent instead (the pre-update behaviour).
void sendAvailableCommands(int sock, sockaddr_in clientAddr, ClientState& state) {
    (void)sock; (void)clientAddr;
    vector<uint8_t> packet;
    writeVarInt(packet, 76);

    if (!getConfig().enableCommands) {
        for (int i = 0; i < 9; i++) writeVarInt(packet, 0); // original empty packet, unchanged
        queueGamePacket(state, packet);
        cout << "[Bedrock] Sent empty AvailableCommands (enable-commands=false)\n";
        return;
    }

    const auto& cmds = getBuiltinCommands();
    writeVarInt(packet, 0); // enum values
    writeVarInt(packet, 0); // chained subcommand values
    writeVarInt(packet, 0); // suffixes
    writeVarInt(packet, 0); // enums
    writeVarInt(packet, 0); // chained subcommands
    writeVarInt(packet, (uint32_t)cmds.size()); // commands
    for (const auto& c : cmds) {
        writeString(packet, c.name);
        writeString(packet, c.description);
        writeUShort(packet, 0);            // flags
        writeString(packet, "any");        // permission level
        writeUInt(packet, 0xFFFFFFFFu);    // aliases offset: none
        writeVarInt(packet, 0);            // chained subcommand offsets
        writeVarInt(packet, 1);            // overloads: one
        writeBool(packet, false);          //   chaining
        writeVarInt(packet, 0);            //   parameters: none
    }
    writeVarInt(packet, 0); // dynamic (soft) enums
    writeVarInt(packet, 0); // constraints

    queueGamePacket(state, packet);
    cout << "[Bedrock] Sent AvailableCommands (" << cmds.size() << " commands)\n";
}
