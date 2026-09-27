#include "TextPacket.h"
#include "DataTypeHelper.h"
#include "GamePacketSender.h"
#include <iostream>

using namespace std;

void sendJoinMessage(int sock, sockaddr_in clientAddr, ClientState& state, const string& playerName) {
    vector<uint8_t> packet;
    writeVarInt(packet, 9);

    writeBool(packet, true);    // needs_translation
    writeUByte(packet, 2);      // category = parameters
    writeUByte(packet, 2);      // type = translation

    writeString(packet, "%multiplayer.player.joined"); // message (translation key)
    writeVarInt(packet, 1); // parameters: array count = 1
    writeString(packet, playerName);

    writeString(packet, ""); // xuid
    writeString(packet, ""); // platform_chat_id
    writeBool(packet, false); // has_filtered_message (false -> filtered_message omitted)

    queueGamePacket(state, packet);
    cout << "[Bedrock] Sent join message for " << playerName << "\n";
}
