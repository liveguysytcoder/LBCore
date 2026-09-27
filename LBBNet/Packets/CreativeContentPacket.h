#pragma once
#include <sys/socket.h>
#include <arpa/inet.h>
#include "../../LBNet/Server/Clients.h"

using namespace std;

// Sends CreativeContent (145 / 0x91), EMPTY -- this is what gophertunnel
// actually sends automatically during the StartGame handshake (see
// minecraft/conn.go: `&packet.CreativeContent{}`), before the client has
// even sent SetLocalPlayerAsInitialised.
void sendCreativeContent(int sock, sockaddr_in clientAddr, ClientState& state);

// Sends CreativeContent again, this time with the real 41-item starter
// list. Confirmed against Dragonfly's own session.New() to belong AFTER
// SetLocalPlayerAsInitialised, during session/world setup -- called from
// sendSpawnSequencePhase2().
void sendCreativeContentWithItems(int sock, sockaddr_in clientAddr, ClientState& state);
