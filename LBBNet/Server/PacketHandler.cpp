#include "PacketHandler.h"
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <vector>
#include <utility>
#include "../Packets/DataTypeHelper.h"
#include "../Packets/NetworkSettingsPacket.h"
#include "../Packets/Compression.h"
#include "../Packets/LoginPacket.h"
#include "../Packets/ResourcePackClientResponsePacket.h"
#include "../Packets/HandshakePacket.h"
#include "../Packets/Encryption.h"
#include "../Packets/ChunkRadiusUpdatedPacket.h"
#include "../Packets/LevelChunkPacket.h"
#include "../Packets/SubChunkPacket.h"
#include "../Packets/GamePacketSender.h"
#include "../Packets/PlayerAuthInputPacket.h"
#include "../Packets/TickSyncPacket.h"
#include "../Packets/Multiplayer.h"
#include "../Packets/Chat.h"
#include <sys/socket.h>
#include <arpa/inet.h>

using namespace std;

void handleGamePacket(int sock, sockaddr_in clientAddr, ClientState& state, const vector<uint8_t>& data) {
    if (data.empty()) return;

    size_t offset = 0;
    // Gamepacket header: 10-bit packet id + 2-bit sender subclient + 2-bit
    // target subclient, packed into a single varint (see wiki.bedrock.dev's
    // "Gamepacket Header" section). We don't use subclients, so just mask
    // out the low 10 bits for the id.
    uint32_t header = readVarInt(data, offset);
    uint32_t packetID = header & 0x3FF;

    cout << "[Bedrock] Packet ID: 0x"
         << hex << setw(2) << setfill('0') << packetID
         << dec << " (payload " << (data.size() - offset) << " bytes)\n";

    switch (packetID) {
        case 0xC1: { // RequestNetworkSettings
            uint32_t protocolVersion = readUIntBE(data, offset); // Big-Endian int32
            cout << "[Bedrock] RequestNetworkSettings protocol=" << protocolVersion << "\n";
            sendNetworkSettings(sock, clientAddr, state);
            break;
        }

        case 0x01:
            cout << "[Bedrock] Login\n";
            handleLogin(sock, clientAddr, state, data, offset);
            break;

        case 0x04:
            cout << "[Bedrock] ClientToServerHandshake\n";
            handleClientToServerHandshake(sock, clientAddr, state);
            break;

        case 0x08:
            cout << "[Bedrock] ResourcePackClientResponse\n";
            handleResourcePackClientResponse(sock, clientAddr, state, data, offset);
            break;

        case 0x81: { // ClientCacheStatus
            // A real client sends this right after ResourcePacksInfo/Stack,
            // often batched together with its ClientToServerHandshake reply
            // -- this is expected, normal traffic, not an error. Single bool
            // field: whether the client wants the server to use the blob
            // cache system for chunk data (irrelevant until chunk sending
            // is implemented, but worth recording and no longer logging as
            // "unknown").
            bool cacheEnabled = readBool(data, offset);
            state.clientCacheEnabled = cacheEnabled;
            cout << "[Bedrock] ClientCacheStatus: enabled=" << (cacheEnabled ? "true" : "false") << "\n";
            break;
        }

        case 0x45: { // RequestChunkRadius
            // Sent by the client early in the connection to ask for a
            // specific view distance. Real servers reply with
            // ChunkRadiusUpdated confirming the radius actually granted.
            //
            // Checked against gophertunnel's current RequestChunkRadius
            // struct directly (request_chunk_radius.go):
            //   ChunkRadius    int32 // io.Varint32   -- SIGNED zigzag varint
            //   MaxChunkRadius uint8 // io.Uint8      -- single raw byte
            // FIXED: MaxChunkRadius was being read as a second ZigZag32
            // varint, but the real field is a plain unsigned byte (Uint8),
            // not a varint at all. That happened to not desync anything
            // else in this packet (it's the last field), but it produced a
            // wrong logged value and, more importantly, wasn't an "exact
            // field" match -- readUByte is now used, matching the real
            // wire type.
            int32_t requestedRadius = readZigZag32(data, offset);
            // MaxChunkRadius is only sent by newer clients; guard the read
            // in case an older client's packet genuinely ends here.
            int32_t maxRadius = requestedRadius;
            if (offset < data.size()) maxRadius = readUByte(data, offset);
            cout << "[Bedrock] RequestChunkRadius: requested=" << requestedRadius
                 << " max=" << maxRadius << "\n";

            // FIXED: this used to echo requestedRadius straight back,
            // confirming a view distance (e.g. 8 chunks) far larger than
            // what the chunk-send loop actually delivers (a fixed 3x3 /
            // radius-1 patch). The client then waited for the rest of the
            // radius it was told it had, never got it, and disconnected
            // cleanly. Grant the min of what was asked for and what we can
            // actually deliver -- see kSentChunkRadius's comment in
            // LevelChunkPacket.h, which is now the one place this number
            // is defined.
            int32_t grantedRadius = min(requestedRadius, kSentChunkRadius);
            sendChunkRadiusUpdated(sock, clientAddr, state, grantedRadius);

            // This is also the real client's cue that it's ready for the
            // rest of the spawn sequence -- see sendSpawnSequence()'s
            // comment in ResourcePackClientResponsePacket.cpp for why this
            // moved here instead of firing unconditionally after StartGame.
            sendSpawnSequence(sock, clientAddr, state);
            break;
        }

        case 0xAF: { // SubChunkRequest (id 175; was wrongly 0xA2) -- kept as harmless dead code: a real
            // vanilla server capture confirmed LevelChunk delivers all
            // terrain inline (see LevelChunkPacket.cpp's header comment)
            // and a real client never actually sends this packet. Left in
            // in case some other client implementation does use it.
            // Field-order confidence notes are in SubChunkPacket.cpp's
            // header comment. Parsed as: Dimension (zigzag32), base
            // Position (3x zigzag32), then a VarInt count of relative
            // (dx,dy,dz) signed-byte offsets.
            int32_t dimension = readZigZag32(data, offset);
            int32_t baseX = readInt(data, offset); // SubChunkPos = 3 fixed int32 in 2193
            int32_t baseY = readInt(data, offset);
            int32_t baseZ = readInt(data, offset);
            uint32_t count = readVarInt(data, offset);

            vector<SubChunkOffsetRequest> offsets;
            offsets.reserve(count);
            for (uint32_t i = 0; i < count; i++) {
                int8_t dx = readByte(data, offset);
                int8_t dy = readByte(data, offset);
                int8_t dz = readByte(data, offset);
                offsets.push_back({dx, dy, dz});
            }

            cout << "[Bedrock] SubChunkRequest: dimension=" << dimension
                 << " base=(" << baseX << "," << baseY << "," << baseZ << ")"
                 << " count=" << count << "\n";

            sendSubChunkResponse(sock, clientAddr, state, dimension, baseX, baseY, baseZ, offsets);
            flushGamePackets(sock, clientAddr, state);
            break;
        }

        case 0x90: // PlayerAuthInput -- real clients send this continuously
            // (many times/sec) once actually in the world. See
            // PlayerAuthInputPacket.h/.cpp for the parsed field layout and
            // why an MVP (front-block-only) parse is safe here.
            handlePlayerAuthInput(sock, clientAddr, state, data, offset);
            break;

        case 0x17: // TickSync -- ADDED: previously completely unhandled,
            // not even acknowledged. Real clients use this as a request/
            // response timing handshake right around when they start
            // sending PlayerAuthInput; total silence here is a real
            // candidate for a real client concluding the connection is
            // unhealthy shortly after spawning. See TickSyncPacket.h for
            // the response strategy (echo, not a fabricated tick count).
            handleTickSync(sock, clientAddr, state, data, offset);
            break;

        case 0x21: {
            // Interact -- ADDED real parsing (was previously just logged
            // and ignored with no field visibility at all). Fields
            // confirmed against protocol.json's packet_interact for
            // 1.26.40: action_id (mapper u8: leave_vehicle=3,
            // mouse_over_entity=4, npc_open=5, open_inventory=6),
            // target_entity_id (varint64), has_position (bool), position
            // (vec3f, only if has_position). Safe to parse without risk
            // of desyncing anything else in the batch -- see
            // PlayerAuthInputPacket.h's comment for why (each packet in a
            // batch is already sliced to its own declared length before
            // this switch ever sees it).
            //
            // This is diagnostic-only for now: logs which action_id a
            // real client actually sends right before it disconnected in
            // an earlier test, so the next real-client test tells us
            // definitively rather than guessing which of the 4 possible
            // actions it was.
            uint8_t actionId = readUByte(data, offset);
            uint64_t targetEntityId = readVarInt64(data, offset);
            bool hasPosition = readBool(data, offset);
            float posX = 0, posY = 0, posZ = 0;
            if (hasPosition) {
                posX = readFloat(data, offset);
                posY = readFloat(data, offset);
                posZ = readFloat(data, offset);
            }
            const char* actionName = "unknown";
            switch (actionId) {
                case 3: actionName = "leave_vehicle";     break;
                case 4: actionName = "mouse_over_entity";  break;
                case 5: actionName = "npc_open";           break;
                case 6: actionName = "open_inventory";     break;
            }
            cout << "[Bedrock] Interact: action_id=" << (int)actionId << " (" << actionName << ")"
                 << " target_entity_id=" << targetEntityId
                 << " has_position=" << (hasPosition ? "true" : "false");
            if (hasPosition) {
                cout << " position=(" << posX << ", " << posY << ", " << posZ << ")";
            }
            cout << " -- not acted on yet, parsed for diagnosis only\n";
            break;
        }

        case 0x09: // Text -- inbound chat: validated, rate-limited, broadcast
            handleText(sock, clientAddr, state, data, offset);
            break;

        case 0x4D: // CommandRequest -- built-in commands, replies with CommandOutput
            handleCommandRequest(sock, clientAddr, state, data, offset);
            break;

        case 0x2C: // Animate -- arm swing, forwarded to the other players
            handleAnimate(state, data, offset);
            break;

        case 0x98: // EmoteList -- the client's equipped emotes; nothing to do
            break;

        case 0x138: // ServerboundLoadingScreen -- marks when the client's
            // loading screen starts/ends. No reply needed; useful later as
            // a real "player has finished loading" signal instead of
            // guessing from RequestChunkRadius alone.
            cout << "[Bedrock] ServerboundLoadingScreen (not yet handled, ignoring)\n";
            break;

        case 0x71: { // SetLocalPlayerAsInitialised
            // Sent by the client in response to PlayStatus(PlayerSpawn) --
            // marks the point where the client is fully initialised and
            // will stop discarding packets it wasn't expecting.
            //
            // FIELDS: confirmed directly against gophertunnel's
            // set_local_player_as_initialised.go -- the packet carries
            // exactly one field, EntityRuntimeID, written via
            // io.ActorRuntimeID (an UNSIGNED varint64), the same encoding
            // StartGame's own Runtime ID uses. This project always assigns
            // runtime ID 1 to the single player on a connection (see
            // StartGamePacket.cpp's writeVarInt64(packet, 1)), so this is
            // the client echoing that same id back. Previously this field
            // was never actually read -- the packet was only logged by ID.
            // Reading it now and comparing against the id we handed out
            // mirrors gophertunnel's own handleSetLocalPlayerAsInitialised,
            // which treats a mismatch here as a protocol error worth
            // surfacing rather than silently ignoring.
            uint64_t initialisedRuntimeId = readVarInt64(data, offset);
            constexpr uint64_t kExpectedRuntimeId = 1; // matches StartGame's Runtime ID
            if (initialisedRuntimeId != kExpectedRuntimeId) {
                cout << "[Bedrock] SetLocalPlayerAsInitialised: entity runtime ID mismatch "
                        "(expected " << kExpectedRuntimeId << " from StartGame, got "
                     << initialisedRuntimeId << ")\n";
            } else {
                cout << "[Bedrock] SetLocalPlayerAsInitialised(entityRuntimeId="
                     << initialisedRuntimeId << ") -- client has finished spawning\n";
            }
            sendSpawnSequencePhase2(sock, clientAddr, state, "SetLocalPlayerAsInitialised");
            break;
        }

        default:
            cout << "[Bedrock] Unknown packet ID: 0x"
                 << hex << packetID << dec << "\n";
            break;
    }
}

void handleBedrockPacket(int sock, sockaddr_in clientAddr, ClientState& state, const vector<uint8_t>& data) {
    // Real wire layout (confirmed against an actual client's
    // RequestNetworkSettings frame — fe 06 c1 01 00 00 03 e9):
    //   data[0]   = 0xFE   (marks this as a Bedrock game packet batch)
    //   data[1..] = one or more [varint length][packet bytes], now
    //               real-zlib-compressed as of NetworkSettingsPacket.cpp's
    //               switch away from CompressionAlgorithm=none (see that
    //               file's comments for why "none" was abandoned).
    //
    // Earlier note (kept for history): two separate attempts at a
    // compression-algorithm-id-byte convention both regressed real-client
    // behavior and were reverted. That finding is unrelated to the current
    // -3 inflate error below, which is about zlib wrapper format, not an
    // extra id byte.
    if (data.size() < 2 || data[0] != 0xFE) {
        cout << "[Bedrock] Malformed batch (missing 0xFE marker)\n";
        return;
    }

    vector<uint8_t> body(data.begin() + 1, data.end());

    vector<uint8_t> decompressed;
    const vector<uint8_t>* payload = &body;

    // Once ServerToClientHandshake has been sent (see HandshakePacket.cpp),
    // every subsequent batch from this client — starting with its
    // ClientToServerHandshake reply — arrives AES-256-CFB8 encrypted with
    // an 8-byte checksum trailer. Per the wiki, packet data is fed to zlib
    // "after being decrypted if encryption is enabled" -- decrypt first,
    // THEN decompress, not one or the other. These used to be an if/else,
    // which silently dropped decompression entirely once encryption turned
    // on (compression and encryption are both active at the same time for
    // the rest of a real connection, not mutually exclusive).
    if (state.encryptionEnabled) {
        if (!decryptGamePacket(state, body)) {
            cout << "[Bedrock] Failed to decrypt/verify batch (bad checksum), dropping\n";
            return;
        }
        // body now holds the decrypted batch with the trailer stripped.
    }
    if (state.compressionEnabled) {
        // Empirically confirmed via packet-logger capture (real bytes,
        // not guessed): decompressing from body[0] fails ("invalid stored
        // block lengths"), but decompressing from body[1] onward parses
        // as a genuinely valid raw-deflate stream with zero errors.
        // body[0] is a 1-byte compression-algorithm-ID prefix the client
        // adds ahead of its actual compressed payload -- 0x00 matching
        // exactly the algorithm=0/zlib negotiated in NetworkSettings.
        // This is the same byte a past attempt in this file tried and
        // reverted, but that was tested back when compression was always
        // negotiated as "none" -- the byte's real behavior only shows up
        // once real compression is active, which it is now.
        if (body.empty()) {
            cout << "[Bedrock] Empty compressed batch, dropping\n";
            return;
        }
        uint8_t algoId = body[0];
        vector<uint8_t> compressedPayload(body.begin() + 1, body.end());

        cout << "[Compression] algoId=" << (int)algoId
             << ", payload " << compressedPayload.size() << " bytes, first 32:\n  ";
        for (size_t i = 0; i < compressedPayload.size() && i < 32; i++)
            cout << hex << setw(2) << setfill('0') << (int)compressedPayload[i] << " ";
        cout << dec << endl;

        // algoId 255 (0xFF, the low byte of the protocol-level
        // CompressionAlgorithmNone = 0xffff) means the CLIENT chose not to
        // compress THIS particular batch -- real clients do this for small
        // batches (confirmed directly: a 2-byte ClientToServerHandshake
        // reply, raw bytes "01 04", arrived with algoId=255) even when the
        // connection overall negotiated real compression (algoId=0/zlib)
        // via NetworkSettings. The two are independent per-batch choices,
        // not a connection-wide constant. Previously this code always
        // called zlibDecompress() regardless of algoId, which fed raw,
        // already-uncompressed bytes into zlib inflate and failed
        // immediately ("zlib inflate failed with code -5" / Z_DATA_ERROR)
        // -- silently dropping every small packet the client ever sent
        // this way, starting with its very first post-Login reply. Only
        // decompress when algoId actually says this payload is compressed.
        if (algoId == 255) {
            decompressed = std::move(compressedPayload);
        } else {
            if (!zlibDecompress(compressedPayload, decompressed)) {
                cout << "[Bedrock] Failed to zlib-decompress batch (algoId=" << (int)algoId << ")\n";
                return;
            }
        }
        payload = &decompressed;
    }

    size_t offset = 0;
    while (offset < payload->size()) {
        uint32_t length = readVarInt(*payload, offset);
        if (offset + length > payload->size()) {
            cout << "[Bedrock] Truncated packet in batch, dropping remainder\n";
            break;
        }

        vector<uint8_t> packetBytes(payload->begin() + offset, payload->begin() + offset + length);
        offset += length;

        // A malformed/truncated packet must never crash the server: the read
        // helpers now throw on over-reads and it is caught here, per packet.
        try {
            handleGamePacket(sock, clientAddr, state, packetBytes);
        } catch (const std::exception& e) {
            cout << "[Bedrock] Dropped malformed packet (" << e.what() << ")\n";
        }
    }
}
