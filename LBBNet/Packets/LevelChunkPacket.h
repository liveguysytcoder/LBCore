#pragma once
#include <cstdint>
#include <vector>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "../../LBNet/Server/Clients.h"

using namespace std;

// FIXED (root cause of the "world never renders / clean disconnect right
// after chunks" bug): the chunk-send loop in
// ResourcePackClientResponsePacket.cpp only ever sends a 3x3 grid (radius
// 1 in chunks) around spawn, but ChunkRadiusUpdatedPacket.cpp was echoing
// back whatever radius the client actually requested (8, in the captured
// run) -- telling the client "your full 8-chunk view distance is granted"
// while only ever delivering 1. A real client trusts ChunkRadiusUpdated to
// know how much terrain to expect before it considers itself loaded; it
// waits for the rest of its granted radius, never gets it, and disconnects
// cleanly (exactly what lbcore.log.txt showed: 9 LevelChunk sends, then
// ID_DISCONNECTION_NOTIFICATION). Compare against packet-logger_log.txt's
// real Dragonfly capture: chunk_radius_update granted 10, and the server
// followed through with 349 level_chunk packets to match -- radius granted
// and radius actually delivered must always agree.
//
// kSentChunkRadius is the single source of truth for "how many chunks (in
// chunks, not blocks) this server actually sends around spawn." It's used
// to clamp ChunkRadiusUpdated's reply AND to size NetworkChunkPublisherUpdate
// AND to bound the send loop itself, so these three can never drift apart
// again the way they just did. Raise this (and widen the send loop to
// match) once more than a 3x3 patch is actually being generated.
//
// RAISED from 1 to 4: a real client (lbcore_log.txt) requested radius=7
// and was only granted radius=1 (a 3x3 patch, 9 chunks). Real Bedrock
// clients also enforce their own minimum simulation distance (commonly 4
// chunks) around the player independently of render distance -- if the
// granted radius can't satisfy that minimum, the client never finishes
// its "Generating world" screen no matter how correct the 9 chunks it did
// get are, and eventually times out and disconnects. 4 is the smallest
// radius that should clear that common minimum; it can go higher later
// once real (non-flat-placeholder) terrain generation exists.
constexpr int32_t kSentChunkRadius = 4;

// Sends LevelChunk (58 / 0x3A) for chunk (chunkX, chunkZ) in INLINE mode: the
// packet carries kInlineSubChunkCount sub-chunks (Y index -4..0) plus the
// biome column, and the client is NOT expected to send SubChunkRequests.
// The single populated sub-chunk (index 0, world Y 0-15) is superflat
// terrain: bedrock, dirt, dirt, grass, then air.
//
// This relies on StartGamePacket's BlockNetworkIdsAreHashes flag being set
// to true: instead of raw integer "runtime IDs" that must match the exact
// connecting client's compiled-in block table (a table this project has
// no copy of -- see BlockHash.h for why that's normally a real risk, per
// github.com/Sandertv/gophertunnel/issues/273), each palette entry here is
// a stable hash of the block's name+states, computed the same way the
// client computes it from its own registry. See BlockHash.h/.cpp.

// Overworld height is 384 blocks = 24 sub-chunks, indexed -4..19 (Y -64..319).
constexpr int    kOverworldSubChunkSections = 24;
constexpr int8_t kLowestSubChunkIndex       = -4;
constexpr int8_t kTerrainSubChunkIndex      = 0;   // the one populated layer
constexpr uint32_t kInlineSubChunkCount     = 5;   // indices -4..0

// Shared sub-chunk / biome serialisers (also used by SubChunkPacket.cpp).
// Network format, sub-chunk version 9: [9][storage count][Y index][storages].
void appendEmptySubChunk(std::vector<uint8_t>& buf, int8_t yIndex);
void appendTerrainSubChunk(std::vector<uint8_t>& buf, int8_t yIndex);
// One full uniform biome storage followed by "same as previous" markers for
// the rest of the column (kOverworldSubChunkSections storages in total).
void appendBiomeColumn(std::vector<uint8_t>& buf);

void sendLevelChunk(int sock, sockaddr_in clientAddr, ClientState& state, int32_t chunkX, int32_t chunkZ);
