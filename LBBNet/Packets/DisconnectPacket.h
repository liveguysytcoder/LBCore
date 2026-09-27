#pragma once
#include <string>
#include <cstdint>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "../../LBNet/Server/Clients.h"

using namespace std;

// Disconnect (packet id 5). Wire layout verified against gophertunnel
// v1.62.0 (protocol 2193) Disconnect.Marshal():
//   reason        varint32 (zigzag)  -- DisconnectFailReason enum value
//   hideScreen    bool
//   if (!hideScreen): message string, filtered message string
// Common reasons (from Mojang's DisconnectFailReason enum): 2 NoPermissions,
// 25 ServerFull, 55 Kicked, 68 Shutdown.
// Queues AND flushes -- the client is told immediately.
void sendDisconnect(int sock, sockaddr_in clientAddr, ClientState& state,
                    int32_t reason, const string& message, bool hideScreen = false);

// Sends Disconnect to every logged-in client (used on server shutdown).
void disconnectAllClients(int32_t reason, const string& message);
