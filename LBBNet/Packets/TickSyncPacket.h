#pragma once
#include <vector>
#include <cstdint>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "../../LBNet/Server/Clients.h"

using namespace std;

// Handles gamepacket id 23 (0x17), TickSync. Field layout confirmed
// against minecraft-data's protocol.json for bedrock 1.26.40
// (types.packet_tick_sync): request_time (li64), response_time (li64).
//
// This was previously never handled at all -- not even acknowledged. Real
// clients use TickSync as a request/response timing handshake; total
// silence in response to it is a real candidate for why a real client,
// after briefly spawning and sending a couple of PlayerAuthInput packets,
// was cleanly disconnecting itself shortly after (client concludes the
// connection is unhealthy).
//
// Response strategy: echo request_time back as both fields. This project
// has no server tick-counter infrastructure yet, so rather than invent a
// fake tick number, this matches what many minimal server implementations
// do -- the primary purpose of TickSync is round-trip timing measurement,
// which an echo satisfies correctly; exact server-tick alignment is a
// separate, larger feature (the same ongoing-tick-loop gap already
// flagged elsewhere) that this deliberately does not try to fake.
void handleTickSync(int sock, sockaddr_in clientAddr, ClientState& state,
                     const vector<uint8_t>& data, size_t& offset);
