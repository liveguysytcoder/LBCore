#pragma once
#include <sys/socket.h>
#include <arpa/inet.h>
#include "../../LBNet/Server/Clients.h"

using namespace std;

// Sends ItemRegistry (162 / 0xA2) to the client — the full vanilla item
// table (name, numeric id, whether it's component-based, and its component
// NBT data where applicable), generated from pmmp/BedrockData's
// required_item_list.json. Real clients crash shortly after spawning in
// without this; an empty list is not sufficient.
void sendItemRegistry(int sock, sockaddr_in clientAddr, ClientState& state);

// Sends ItemRegistry a SECOND time, empty. CONFIRMED against Dragonfly's
// own source (server/server.go, right after conn.StartGameContext(...)
// returns): a real server sends this again immediately after the
// handshake completes, this time carrying only custom/server-defined
// items (none, for this project) rather than the full vanilla table.
void sendEmptyItemRegistry(int sock, sockaddr_in clientAddr, ClientState& state);
