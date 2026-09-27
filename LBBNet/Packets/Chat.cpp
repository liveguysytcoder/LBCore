#include "Chat.h"
#include "DataTypeHelper.h"
#include "GamePacketSender.h"
#include "../../LBNet/Server/Server.h"
#include "../../Config/Config.h"
#include <chrono>
#include <cstdio>
#include <iostream>
#include <utility>

using namespace std;

namespace {

constexpr size_t kMaxChatBytes = 512;      // vanilla chat limit
constexpr int64_t kChatWindowMs = 3000;    // flood guard: at most kChatMaxInWindow
constexpr uint32_t kChatMaxInWindow = 6;   // messages per kChatWindowMs

int64_t nowMs() {
    return chrono::duration_cast<chrono::milliseconds>(
        chrono::steady_clock::now().time_since_epoch()).count();
}

// Removes control characters and Minecraft formatting codes (the section
// sign U+00A7, UTF-8 C2 A7) so players can't inject colours/formatting, then
// trims surrounding spaces and caps the length.
string sanitizeChat(const string& in) {
    string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == 0xC2 && i + 1 < in.size() && (unsigned char)in[i + 1] == 0xA7) { i++; continue; }
        if (c < 0x20 || c == 0x7F) continue;
        out.push_back((char)c);
    }
    size_t a = out.find_first_not_of(' ');
    if (a == string::npos) return "";
    size_t b = out.find_last_not_of(' ');
    out = out.substr(a, b - a + 1);
    if (out.size() > kMaxChatBytes) out.resize(kMaxChatBytes);
    return out;
}

struct OutMsg {
    string text;
    bool success;
    vector<string> params;
};

void sendCommandOutput(int sock, sockaddr_in addr, ClientState& to,
                       const string& originStr, const uint8_t uuid[16],
                       const string& requestId, int64_t playerUniqueId,
                       const vector<OutMsg>& msgs, uint32_t successCount) {
    vector<uint8_t> p;
    writeVarInt(p, 79);                       // CommandOutput
    writeString(p, originStr);                // origin type (string in 2193)
    p.insert(p.end(), uuid, uuid + 16);       // origin UUID (echoed as received)
    writeString(p, requestId);                // request id
    writeInt64(p, playerUniqueId);            // player unique id (always present)
    writeString(p, "alloutput");              // output type (string in 2193)
    writeUInt(p, successCount);               // success count (fixed u32)
    writeVarInt(p, (uint32_t)msgs.size());    // output messages
    for (const auto& m : msgs) {
        writeString(p, m.text);
        writeBool(p, m.success);
        writeVarInt(p, (uint32_t)m.params.size());
        for (const auto& s : m.params) writeString(p, s);
    }
    writeBool(p, false);                      // data set: absent
    queueGamePacket(to, p);
    flushGamePackets(sock, addr, to);
}

string lowerFirstToken(const string& line, size_t& endPos) {
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '/')) i++;
    size_t j = i;
    while (j < line.size() && line[j] != ' ') j++;
    endPos = j;
    string t = line.substr(i, j - i);
    for (auto& ch : t) if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
    return t;
}

} // namespace

const vector<BuiltinCommand>& getBuiltinCommands() {
    static const vector<BuiltinCommand> cmds = {
        {"help",    "Lists the available commands"},
        {"list",    "Lists the players currently online"},
        {"pos",     "Shows your current coordinates"},
        {"version", "Shows the server version"},
    };
    return cmds;
}

void handleText(int sock, sockaddr_in clientAddr, ClientState& state,
                const vector<uint8_t>& data, size_t& offset) {
    (void)sock; (void)clientAddr;
    (void)readBool(data, offset);                 // needs translation
    (void)readUByte(data, offset);                // category
    uint8_t type = readUByte(data, offset);       // text type

    string message;
    switch (type) {
        case 1: case 7: case 8:                   // chat / whisper / announcement: source + message
            (void)readString(data, offset);       // client-supplied source name: never trusted
            message = readString(data, offset);
            break;
        case 0: case 5: case 6: case 9: case 10: case 11: // message only
            message = readString(data, offset);
            break;
        case 2: case 3: case 4: {                 // message + parameters
            message = readString(data, offset);
            uint32_t n = readVarInt(data, offset);
            if (n > 4) throw out_of_range("too many text parameters");
            for (uint32_t i = 0; i < n; i++) (void)readString(data, offset);
            break;
        }
        default:
            return;
    }
    if (type != 1 || !state.spawned) return;      // only ordinary chat from spawned players

    message = sanitizeChat(message);
    if (message.empty()) return;

    int64_t now = nowMs();
    if (now - state.chatWindowStartMs > kChatWindowMs) {
        state.chatWindowStartMs = now;
        state.chatWindowCount = 0;
    }
    if (++state.chatWindowCount > kChatMaxInWindow) {
        cout << "[Chat] Dropped message from " << state.displayName << " (rate limit)\n";
        return;
    }

    // Broadcast with the AUTHENTICATED display name (anti-spoofing).
    vector<uint8_t> p;
    writeVarInt(p, 9);                 // Text
    writeBool(p, false);               // needs translation
    writeUByte(p, 1);                  // category: authored message
    writeUByte(p, 1);                  // type: chat
    writeString(p, state.displayName); // source name
    writeString(p, message);           // message
    writeString(p, state.xuid);        // xuid
    writeString(p, "");                // platform chat id
    writeBool(p, false);               // no filtered message
    forEachClient([&](ClientState& c) {
        if (!c.spawned) return;
        queueGamePacket(c, p);
        flushGamePackets(serverSocket, c.addr, c);
    });
    cout << "[Chat] <" << state.displayName << "> " << message << "\n";
}

void handleCommandRequest(int sock, sockaddr_in clientAddr, ClientState& state,
                          const vector<uint8_t>& data, size_t& offset) {
    string line      = readString(data, offset);
    string originStr = readString(data, offset);
    uint8_t uuid[16];
    for (int i = 0; i < 16; i++) uuid[i] = readUByte(data, offset);
    string requestId = readString(data, offset);
    int64_t playerUid = readInt64(data, offset);
    (void)readBool(data, offset);                 // internal
    (void)readString(data, offset);               // version
    if (!state.spawned) return;

    size_t end = 0;
    string name = lowerFirstToken(line, end);
    cout << "[Command] " << state.displayName << " ran: " << line << "\n";

    vector<OutMsg> out;
    uint32_t ok = 1;
    if (name == "help") {
        out.push_back({"Available commands:", true, {}});
        for (const auto& c : getBuiltinCommands())
            out.push_back({string("/") + c.name + " - " + c.description, true, {}});
    } else if (name == "list") {
        vector<string> names;
        forEachClient([&](ClientState& c) { if (c.spawned) names.push_back(c.displayName); });
        string joined;
        for (size_t i = 0; i < names.size(); i++) joined += (i ? ", " : "") + names[i];
        out.push_back({"There are " + to_string(names.size()) + "/" +
                       to_string(getConfig().maxPlayers) + " players online:", true, {}});
        out.push_back({joined, true, {}});
    } else if (name == "pos") {
        char buf[96];
        snprintf(buf, sizeof(buf), "Position: %.1f, %.1f, %.1f", state.posX, state.posY - 1.62f, state.posZ);
        out.push_back({buf, true, {}});
    } else if (name == "version") {
        out.push_back({"LBCore for Minecraft Bedrock " + getConfig().versionName +
                       " (protocol " + to_string(getConfig().protocolVersion) + ")", true, {}});
    } else {
        ok = 0;
        out.push_back({"commands.generic.unknown", false, {name}});
    }
    sendCommandOutput(sock, clientAddr, state, originStr, uuid, requestId, playerUid, out, ok);
}
