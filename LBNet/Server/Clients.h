#pragma once
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <cstdint>
#include <functional>
#include <arpa/inet.h>
#include <sys/socket.h>

using namespace std;

// Fragmented (split) packet reassembly buffer for a single splitId.
struct SplitPacketBuffer {
    uint32_t splitCount = 0;
    map<uint32_t, vector<uint8_t>> fragments;
};

// All RakNet-level state that must be tracked PER CONNECTED CLIENT.
//
// Previously this project kept these counters as global variables shared by
// every client on the server. That corrupts state the moment a second
// client connects: sequence numbers, reliable-message indices, ordering
// indices, ACK/NACK bookkeeping and split-packet reassembly buffers would
// all be mixed between unrelated connections. Each client now gets its own
// isolated copy of everything a RakNet connection needs to track.
struct ClientState {
    uint16_t index = 0;

    // Outgoing FrameSetPacket sequence number (datagram sequence number).
    uint32_t nextSeqNum = 0;
    // Outgoing reliable-message index (capsule index for reliable frames).
    uint32_t nextReliableIndex = 0;
    // Outgoing ordering index (ordering channel 0).
    uint32_t nextOrderingIndex = 0;

    // Incoming datagram sequence tracking, used to detect gaps for NACKs.
    uint32_t expectedSeq = 0;
    bool hasReceivedAny = false;
    set<uint32_t> ackedSeqs;

    // Outgoing datagrams kept around so we can resend them if the client NACKs.
    map<uint32_t, vector<uint8_t>> sentPackets;

    // Packet bodies queued via queueGamePacket() (GamePacketSender.cpp),
    // each already length-prefixed, waiting for the next flushGamePackets()
    // call to combine them all into one encrypted batch and send it --
    // matching gophertunnel's real bufferedSend/Flush() model instead of
    // sending every packet as its own separate encrypted RakNet message.
    vector<uint8_t> pendingBatch;

    // Split-packet reassembly buffers, keyed by splitId.
    map<uint16_t, SplitPacketBuffer> splitBuffers;

    // Reliable-Ordered delivery enforcement for INCOMING frames, ordering
    // channel 0 (the only channel this project's sender ever uses -- see
    // GamePacketSender.cpp). Confirmed as a real, previously-missing piece:
    // frames were being handed to processFrameContent() the instant they
    // physically arrived, with orderingIndex read off the wire but never
    // actually checked against anything. That let frames a real client
    // sent in one order (e.g. a resource-pack response, then later
    // something else) reach the game layer in a DIFFERENT order if they
    // happened to arrive out of sequence -- observed directly as a
    // ResourcePackClientResponse(Completed) being processed before an
    // earlier ResourcePackClientResponse(HaveAllPacks) the client had
    // actually sent first. nextExpectedOrderingIndex is the next index
    // this client is allowed to advance past; orderedFrameBuffer holds
    // fully-reassembled (split or not) payloads that arrived early,
    // waiting their turn. See deliverOrdered() in PacketHandler.cpp.
    uint32_t nextExpectedOrderingIndex = 0;
    map<uint32_t, vector<uint8_t>> orderedFrameBuffer;

    // MTU this client negotiated during the OpenConnectionRequest1/Reply1 +
    // OpenConnectionRequest2/Reply2 handshake (see OpenConnectionRequest2.cpp).
    // Used by sendGamePacket() to decide when an outgoing game packet body is
    // too big for one datagram and has to be split across multiple RakNet
    // split-packet frames instead of being handed to sendto() as one
    // oversized datagram. Defaulted to a conservative value in case a game
    // packet somehow needs sending before the handshake finished.
    uint16_t mtu = 1400;

    // Outgoing split-packet id counter. Each logical game packet body that
    // needs splitting gets the next value here, shared by all of its
    // fragments (splitId), so the receiving end can tell fragments of
    // different oversized packets apart.
    uint16_t nextSplitId = 0;

    // Whether NetworkSettings has already been negotiated (compression, etc).
    bool compressionEnabled = false;

    // Identity extracted from the Login packet's certificate chain (see
    // LBBNet/Packets/LoginPacket.h/.cpp). loggedIn stays false until a
    // Login packet with a valid identity token has been processed.
    bool loggedIn = false;
    string displayName;
    string xuid;
    string identityUUID;

    // Skin/appearance data extracted from the Login packet's client-data
    // JWT (see extractClientSkinData() in LBBNet/Packets/LoginPacket.h/.cpp).
    // Kept as flat primitive fields here (mirroring displayName/xuid/
    // identityUUID above) rather than a LBBNet-layer struct, so this
    // RakNet-layer header doesn't have to depend on the game-layer one.
    // hasSkinData stays false (and every field below stays at its
    // placeholder default) for connections that sent no usable clientData
    // -- PlayerListPacket.cpp falls back to a baked default skin in that
    // case rather than sending an empty/invalid one.
    bool hasSkinData = false;
    string skinId;
    string skinResourcePatch;       // decoded JSON text
    int32_t skinImageWidth = 0;
    int32_t skinImageHeight = 0;
    vector<uint8_t> skinImageData;  // decoded raw RGBA bytes
    string capeId;
    int32_t capeImageWidth = 0;
    int32_t capeImageHeight = 0;
    vector<uint8_t> capeImageData;  // decoded raw RGBA bytes; empty if no cape
    string skinGeometryData;        // decoded JSON text
    string skinGeometryDataVersion;
    string skinAnimationData;       // decoded JSON text
    string armSize = "wide";        // "wide" or "slim"
    bool personaSkin = false;
    bool premiumSkin = false;
    bool capeOnClassicSkin = false;

    // Whether the client asked to use the blob-cache system (ClientCacheStatus,
    // gamepacket id 0x81 / 129 -- sent right after the resource pack stack,
    // per gophertunnel's conn.go: `conn.expect(IDResourcePackClientResponse,
    // IDClientCacheStatus)`). Not acted on yet since chunk sending isn't
    // implemented, but recorded for when it is.
    bool clientCacheEnabled = false;

    // Set once sendStartGame()'s full spawn sequence has actually gone
    // out for this client, so it only ever happens once. Needed because
    // the spawn sequence can now be triggered from two places: a real
    // ResourcePackClientResponse(Completed) from the client, or — since
    // this server never serves any packs — proactively right after
    // ResourcePacksStack (see completeResourcePackStageAndStartGame() in
    // ResourcePackClientResponsePacket.cpp/.h and its call site in
    // ResourcePackStackPacket.cpp). Without this guard a client that takes
    // both paths (sends Stack, then also sends a Completed response out of
    // habit) would get StartGame/chunks/inventory sent twice.
    bool startGameSent = false;

    // Set once the chunks/inventory/PlayStatus(PlayerSpawn) part of the
    // spawn sequence has gone out, so a client that sends RequestChunkRadius
    // more than once (real clients can re-request a different radius later)
    // doesn't get the whole world+spawn burst repeated. See
    // sendSpawnSequence() in ResourcePackClientResponsePacket.h/.cpp and its
    // call site in PacketHandler.cpp's RequestChunkRadius (0x45) case.
    bool spawnSequenceSent = false;
    // Two-phase spawn: phase 1 (PlayerSpawn + CreativeContent) is sent on
    // RequestChunkRadius; phase 2 (everything else) waits for the client's
    // SetLocalPlayerAsInitialised. See sendSpawnSequencePhase2().
    bool spawnPhase2Sent = false;

    // Login encryption handshake state (see LBBNet/Packets/Encryption.h and
    // HandshakePacket.h/.cpp). Only turned on for clients that logged in
    // with a real Xbox Live identity chain — e.g. the official Minecraft
    // client signed in and playing online, as opposed to older
    // offline/no-chain test logins this project also still supports.
    bool encryptionEnabled = false;
    vector<uint8_t> encryptionKey; // SHA-256(salt + ECDH secret), 32 bytes
    uint64_t sendPacketCounter = 0; // per-direction, feeds the checksum trailer
    uint64_t recvPacketCounter = 0;
    // Opaque EVP_CIPHER_CTX* (OpenSSL). Stored as void* so this header
    // doesn't need to pull in OpenSSL for every file that just wants
    // ClientState's other fields — see Encryption.cpp for the real type
    // and all the crypto logic that touches these.
    void* sendCipherCtx = nullptr; // outgoing (encrypt)
    void* recvCipherCtx = nullptr; // incoming (decrypt)

    // Live position/rotation, updated from PlayerAuthInput (gamepacket id
    // 144 / 0x90) -- see LBBNet/Packets/PlayerAuthInputPacket.h/.cpp. Real
    // clients send this continuously (many times/sec) once actually in the
    // world, so these fields reflect wherever the player currently is/is
    // looking, not just their StartGame spawn position.
    float posX = 0.0f, posY = 0.0f, posZ = 0.0f;
    float yaw = 0.0f, pitch = 0.0f, headYaw = 0.0f;
    uint64_t lastAuthInputTick = 0;
    bool receivedAuthInput = false; // true once the first PlayerAuthInput has arrived
    // Tick this client's NetworkChunkPublisherUpdate was last (re-)sent.
    // Real servers don't send this once and leave it -- see the note in
    // streamChunksAround() in Multiplayer.cpp.
    uint64_t lastPublisherUpdateTick = 0;

    // ---- multiplayer / chunk-streaming state (added for 26.51 update) ----
    // This client's UDP address, stored so the server can send to it from
    // code running on behalf of a DIFFERENT client (chat, other players).
    sockaddr_in addr{};
    // The id every OTHER client uses for this player (AddPlayer/MovePlayer/
    // RemoveActor). A player's own view of itself is always id 1 (StartGame).
    int64_t entityId = 0;
    // True once the full spawn sequence has gone out (safe to send game
    // packets to this client from other clients' handlers).
    bool spawned = false;
    // Chunk the player was last streamed around + chunks the client holds.
    int32_t lastChunkX = 0, lastChunkZ = 0;
    set<pair<int32_t, int32_t>> sentChunks;
    // PlayerAuthInput extras.
    uint32_t authInputCount = 0;
    uint64_t inputFlagMask = 0;
    uint32_t inputMode = 0, playMode = 0;
    float deltaX = 0.0f, deltaY = 0.0f, deltaZ = 0.0f;
    // Chat flood guard (see Chat.cpp).
    int64_t chatWindowStartMs = 0;
    uint32_t chatWindowCount = 0;
};

string getClientKey(sockaddr_in clientAddr);

// Returns the state for this client, creating a fresh one on first contact.
ClientState& getOrCreateClient(sockaddr_in clientAddr);

// Assigns/returns the client's numeric index (kept for compatibility with
// packets like ConnectionRequestAccepted that reference a "system index").
uint16_t addClient(sockaddr_in clientAddr);

void removeClient(sockaddr_in clientAddr);
bool isClientConnected(sockaddr_in clientAddr);
ClientState* findExistingClient(sockaddr_in clientAddr);

// Calls fn for every connected client (used for broadcasts).
void forEachClient(const function<void(ClientState&)>& fn);

// Optional hook invoked from removeClient() just before a client's state is
// destroyed (set by Multiplayer.cpp to announce the departure to others).
extern void (*g_onClientRemoved)(ClientState&);
