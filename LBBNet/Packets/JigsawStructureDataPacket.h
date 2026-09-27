#pragma once
#include <sys/socket.h>
#include <netinet/in.h>
#include "../../LBNet/Server/Clients.h"

// Gamepacket id 313 (0x139), JigsawStructureData. Field layout confirmed
// against minecraft-data's protocol.json for bedrock 1.26.40
// (types.packet_jigsaw_structure_data): a single "structure_data" field of
// type "nbt". A real Dragonfly capture (packet-logger_log.txt) showed
// Dragonfly sends this BEFORE StartGame with an unnamed root TAG_Compound
// containing 4 named, empty TAG_List<TAG_Compound> children -- "processors",
// "template_pools", "jigsaws", "structure_sets" -- not just a bare empty
// compound. This project has no custom jigsaw structures, so it sends the
// same empty-but-correctly-shaped NBT Dragonfly does, matching that capture
// exactly rather than guessing a simpler (and possibly wrong) shape.
void sendJigsawStructureData(int sock, sockaddr_in clientAddr, ClientState& state);
