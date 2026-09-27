#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "../../LBNet/Server/Clients.h"

using namespace std;

// Identity fields pulled out of the Login packet's certificate chain.
struct LoginIdentity {
    bool valid = false;
    string identityUUID; // "identity" claim — the player's Mojang/Xbox UUID
    string displayName;
    string xuid;

    // "identityPublicKey" claim — the client's P-384 public key, base64
    // DER. Present whenever the client logged in with a real Xbox Live
    // identity (an official client, signed in and online); empty for the
    // older offline/no-chain logins this project also still accepts. Its
    // presence is what decides whether handleLogin() runs the encryption
    // handshake (see HandshakePacket.h) before continuing.
    string publicKeyBase64;

    // Set once the chain has been walked and every link's signature
    // verified against the previous link's declared key, with one of
    // those links' key matching Mojang's known root key exactly -- see
    // verifyLoginChain() in LoginPacket.cpp and verifyES384Signature() in
    // Encryption.h. False for offline/no-chain logins (expected) AND for
    // a chain that's malformed or whose signatures don't check out
    // (which a legitimate client would never send).
    bool xboxAuthenticated = false;
};

// Skin/appearance fields pulled out of the Login packet's client-data JWT
// (the second JWT in the Login packet body, separate from the identity
// chain -- see handleLogin()). Field names and shapes verified against
// minecraft-data's own default-skin template
// (data/bedrock/1.16.201/steve.json), which is the exact JSON
// bedrock-protocol's own client login flow sends as ClientData
// (node_modules/bedrock-protocol/src/handshake/login.js) -- this is the
// real schema any Minecraft client's login carries, not a guessed shape.
//
// SkinResourcePatch, SkinGeometryData, SkinData, CapeData and
// SkinAnimationData are all base64-encoded in the JWT payload; every field
// here is stored already-decoded, ready to drop straight into a
// PlayerList "add" record's Skin fields (see sendPlayerListAdd in
// PlayerListPacket.cpp).
//
// NOT extracted yet: AnimatedImageData, PersonaPieces, PieceTintColors --
// this project has no JSON array parser, so PlayerList's corresponding
// arrays are still always sent empty. That's spec-valid (they're
// optional-length arrays) and doesn't block spawning; it just means a
// persona skin's extra layers/tint won't render for OTHER clients seeing
// this player yet. Revisit once a real array parser exists.
struct ClientSkinData {
    bool valid = false; // true once real skin pixel data was found and decoded successfully

    string skinId;
    string skinResourcePatch;      // decoded JSON text, e.g. {"geometry":{"default":"..."}}
    int32_t skinImageWidth = 0;
    int32_t skinImageHeight = 0;
    vector<uint8_t> skinImageData; // decoded raw RGBA bytes, width*height*4 long

    string capeId;
    int32_t capeImageWidth = 0;
    int32_t capeImageHeight = 0;
    vector<uint8_t> capeImageData; // decoded raw RGBA bytes; empty (0x0) if no cape

    string skinGeometryData;        // decoded JSON text (the full bones/mesh geometry)
    string skinGeometryDataVersion; // e.g. "1.21.0"; empty if the client didn't send one
    string skinAnimationData;       // decoded JSON text; usually empty

    string armSize = "wide"; // "wide" or "slim" -- mapped to the wire's u8 in PlayerListPacket.cpp
    bool personaSkin = false;
    bool premiumSkin = false;
    bool capeOnClassicSkin = false;
};

// Parses an already JWT/base64url-decoded client-data JSON payload (see
// decodeJwtPayload() in LoginPacket.cpp) into a ClientSkinData. Returns
// valid=false (all-default fields) if the payload is empty, or if the
// core SkinData/SkinImageWidth/SkinImageHeight fields are missing or
// don't agree with each other in size -- callers should fall back to a
// placeholder skin in that case rather than send an empty/invalid one.
ClientSkinData extractClientSkinData(const string& clientDataPayload);

// Scans every JWT in the chain's JSON (`{"chain":["<jwt>", ...]}`) for the
// one carrying an "extraData" claim, and pulls XUID/displayName/identity
// out of it. Returns a LoginIdentity with valid=false if none is found.
LoginIdentity extractLoginIdentity(const string& chainJson);

// Parses the Login (0x01) game packet body — offset must already point
// past the varint gamepacket header, i.e. at the start of the protocol
// version field — extracts the player's identity from the certificate
// chain, stores it on the client's state, and sends PlayStatus +
// ResourcePacksInfo back so the handshake continues instead of stalling.
void handleLogin(int sock, sockaddr_in clientAddr, ClientState& state,
                  const vector<uint8_t>& data, size_t offset);
