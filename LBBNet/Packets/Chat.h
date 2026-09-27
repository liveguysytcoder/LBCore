#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "../../LBNet/Server/Clients.h"

using namespace std;

// ---------------------------------------------------------------------------
// Chat (Text, id 9) and slash-commands (CommandRequest 77 / CommandOutput 79).
// Layouts verified against gophertunnel v1.62.0 (protocol 2193):
//   Text:           needsTranslation bool, category u8, type u8, per-type
//                   fields, xuid, platformChatId, filteredMessage(optional)
//   CommandRequest: commandLine, origin{originStr, uuid(16), requestId,
//                   playerUniqueId int64}, internal bool, version string
//   CommandOutput:  origin, outputType STRING ("alloutput"...), successCount
//                   u32, messages[{message, success, params[]}], dataSet opt
// ---------------------------------------------------------------------------

struct BuiltinCommand {
    const char* name;
    const char* description;
};
// The commands this server implements (also advertised via AvailableCommands).
const vector<BuiltinCommand>& getBuiltinCommands();

// Inbound Text packet: plain chat is validated, rate-limited and broadcast.
void handleText(int sock, sockaddr_in clientAddr, ClientState& state,
                const vector<uint8_t>& data, size_t& offset);

// Inbound CommandRequest: runs a built-in command and replies with CommandOutput.
void handleCommandRequest(int sock, sockaddr_in clientAddr, ClientState& state,
                          const vector<uint8_t>& data, size_t& offset);
