#include "LoginPacket.h"
#include "DataTypeHelper.h"
#include "Base64Url.h"
#include "JsonLite.h"
#include "PlayStatusPacket.h"
#include "ResourcePacksInfoPacket.h"
#include "HandshakePacket.h"
#include "Encryption.h"
#include <iostream>

using namespace std;

namespace {

// A JWT is "header.payload.signature", each segment base64url-encoded.
// Returns the decoded payload as a string, or "" if the token is malformed.
string decodeJwtPayload(const string& jwt) {
    size_t firstDot = jwt.find('.');
    if (firstDot == string::npos) return "";
    size_t secondDot = jwt.find('.', firstDot + 1);
    if (secondDot == string::npos) return "";

    string payloadB64 = jwt.substr(firstDot + 1, secondDot - firstDot - 1);
    vector<uint8_t> raw = base64UrlDecode(payloadB64);
    return string(raw.begin(), raw.end());
}

// Splits a JWT into its signing input ("header.payload", still
// base64url-encoded, exactly the bytes that were signed) and its
// signature segment (also still base64url-encoded). Returns false if the
// token doesn't have exactly the two dots a JWT requires.
bool splitJwt(const string& jwt, string& outSigningInput, string& outSignatureB64Url) {
    size_t firstDot = jwt.find('.');
    if (firstDot == string::npos) return false;
    size_t secondDot = jwt.find('.', firstDot + 1);
    if (secondDot == string::npos) return false;

    outSigningInput = jwt.substr(0, secondDot);
    outSignatureB64Url = jwt.substr(secondDot + 1);
    return true;
}

// The chain and clientData fields are each prefixed with their length as a
// 4-byte little-endian uint32 — NOT a varint like most other Bedrock
// strings. (Verified against a real client capture: reading this prefix as
// a varint stops after a single byte whenever the low byte's top bit
// happens to be 0, silently truncating the chain JSON to a handful of
// bytes and throwing off every read after it.)
string readLPStringLE(const vector<uint8_t>& data, size_t& offset) {
    if (offset + 4 > data.size()) { offset = data.size(); return ""; }
    uint32_t len = readUInt(data, offset); // little-endian, advances offset by 4
    if (offset + len > data.size()) { offset = data.size(); return ""; }
    string s(data.begin() + offset, data.begin() + offset + len);
    offset += len;
    return s;
}

// Pulls the individual JWT strings out of `{"chain":["...","...","..."]}`.
// We don't have a JSON library in this project, so rather than a full parse
// this just finds the "chain" array and splits its quoted elements — the
// chain is always a flat array of opaque token strings, never nested JSON,
// so this is safe for what the client actually sends.
vector<string> extractChainTokens(const string& chainJson) {
    vector<string> tokens;

    size_t chainKeyPos = chainJson.find("\"chain\"");
    if (chainKeyPos == string::npos) return tokens;

    size_t arrayStart = chainJson.find('[', chainKeyPos);
    if (arrayStart == string::npos) return tokens;
    size_t arrayEnd = chainJson.find(']', arrayStart);
    if (arrayEnd == string::npos) return tokens;

    size_t pos = arrayStart + 1;
    while (pos < arrayEnd) {
        size_t quoteStart = chainJson.find('"', pos);
        if (quoteStart == string::npos || quoteStart > arrayEnd) break;
        size_t quoteEnd = chainJson.find('"', quoteStart + 1);
        if (quoteEnd == string::npos || quoteEnd > arrayEnd) break;

        tokens.push_back(chainJson.substr(quoteStart + 1, quoteEnd - quoteStart - 1));
        pos = quoteEnd + 1;
    }
    return tokens;
}

// Walks a chain array in order and verifies each link's signature against
// the PREVIOUS link's declared "identityPublicKey" -- per Minecraft Wiki's
// Bedrock Login Sequence documentation: "each new claim is validated with
// the key specified in the previous one, with the first key being a self
// signed key." The first token is therefore verified against its OWN
// embedded identityPublicKey (self-signed just proves the client holds
// the matching private key, not identity). Returns true only if every
// signature in the chain checks out AND at least one link was signed
// using a key that exactly matches Mojang's known root key -- meaning
// that link (and everything chained after it) was genuinely produced by
// Mojang's Xbox Live authentication service, not just self-signed by an
// arbitrary client.
//
// NOT covered yet: the newer PlayFab-issued single-token format (the
// "ipt":"PlayFab", "cpk"-bearing path handled earlier in this file) has a
// different structure this function doesn't attempt to verify -- that
// path still extracts identity fields but leaves xboxAuthenticated false,
// same as before this change.
bool verifyLoginChain(const vector<string>& tokens) {
    if (tokens.empty()) return false;

    string previousLinkKey; // key that should verify the CURRENT token
    bool sawMojangKey = false;

    for (size_t i = 0; i < tokens.size(); i++) {
        const string& token = tokens[i];
        string payload = decodeJwtPayload(token);
        if (payload.empty()) return false;

        string thisLinkKey = jsonExtractString(payload, "identityPublicKey");
        if (thisLinkKey.empty()) return false;

        string verifyingKey = (i == 0) ? thisLinkKey : previousLinkKey;

        string signingInput, signatureB64Url;
        if (!splitJwt(token, signingInput, signatureB64Url)) return false;

        if (!verifyES384Signature(signingInput, signatureB64Url, verifyingKey)) {
            return false; // one bad signature invalidates the whole chain
        }

        if (verifyingKey == kMojangRootPublicKeyBase64) {
            sawMojangKey = true;
        }

        previousLinkKey = thisLinkKey;
    }

    return sawMojangKey;
}

} // namespace

LoginIdentity extractLoginIdentity(const string& chainJson) {
    LoginIdentity identity;

    // Newer login format sent by real (Xbox/PlayFab-authenticated) clients:
    // a single wrapped token, e.g. {"AuthenticationType":0,"Token":"<jwt>"}
    // — no "chain" array at all. The identity fields sit at the top level
    // of this token's payload rather than nested under "extraData".
    if (chainJson.find("\"Token\"") != string::npos) {
        string singleToken = jsonExtractString(chainJson, "Token");
        if (!singleToken.empty()) {
            string payload = decodeJwtPayload(singleToken);
            if (!payload.empty()) {
                identity.xuid            = jsonExtractString(payload, "xid");
                identity.displayName     = jsonExtractString(payload, "xname");
                identity.identityUUID    = jsonExtractString(payload, "sub");
                identity.publicKeyBase64 = jsonExtractString(payload, "identityPublicKey");
                if (identity.publicKeyBase64.empty()) {
                    // Current-gen PlayFab-issued tokens (issuer
                    // authorization.franchise.minecraft-services.net,
                    // "ipt":"PlayFab") carry the client's ECDH public key
                    // under "cpk" ("client public key") instead of
                    // "identityPublicKey". Verified against a real client
                    // capture: the token has no "identityPublicKey" field
                    // at all, only "cpk" — missing this is why the
                    // handshake was being skipped for real logins and the
                    // client was left waiting for ServerToClientHandshake
                    // that never came.
                    identity.publicKeyBase64 = jsonExtractString(payload, "cpk");
                }
                identity.valid = !identity.displayName.empty();
                if (identity.valid) return identity;
            }
        }
    }

    // Older chain-array format: {"chain":["<jwt>", ...]}, identity fields
    // nested under whichever link carries an "extraData" claim.
    // Verify the whole chain's signatures up front (see verifyLoginChain
    // above) -- this used to just be extracted and trusted; now a chain
    // whose signatures don't check out, or that was never actually signed
    // by Mojang, is flagged via xboxAuthenticated rather than silently
    // treated the same as a real Xbox Live login.
    vector<string> chainTokens = extractChainTokens(chainJson);
    bool chainVerified = verifyLoginChain(chainTokens);

    for (const string& token : chainTokens) {
        string payload = decodeJwtPayload(token);
        if (payload.empty()) continue;

        // Only the Mojang/Xbox-signed link in the chain carries extraData;
        // the rest just carry public keys used to verify the next link
        // (see verifyLoginChain above). Skip anything that isn't the
        // identity token.
        size_t extraDataPos = payload.find("\"extraData\"");
        if (extraDataPos == string::npos) continue;

        size_t objStart = payload.find('{', extraDataPos);
        if (objStart == string::npos) continue;

        // Find the matching closing brace so we only search within
        // extraData's own object, not the rest of the payload.
        int depth = 0;
        size_t objEnd = string::npos;
        for (size_t i = objStart; i < payload.size(); i++) {
            if (payload[i] == '{') depth++;
            else if (payload[i] == '}') {
                depth--;
                if (depth == 0) { objEnd = i; break; }
            }
        }
        if (objEnd == string::npos) continue;

        string extraDataObj = payload.substr(objStart, objEnd - objStart + 1);

        identity.xuid         = jsonExtractString(extraDataObj, "XUID");
        identity.displayName  = jsonExtractString(extraDataObj, "displayName");
        identity.identityUUID = jsonExtractString(extraDataObj, "identity");
        // "identityPublicKey" sits alongside "extraData" at this JWT's top
        // level, not nested inside it — scope the search to the whole
        // payload rather than extraDataObj.
        identity.publicKeyBase64 = jsonExtractString(payload, "identityPublicKey");
        identity.valid = !identity.displayName.empty();
        identity.xboxAuthenticated = identity.valid && chainVerified;

        if (identity.valid) break; // found the token that matters
    }

    return identity;
}

ClientSkinData extractClientSkinData(const string& payload) {
    ClientSkinData skin;
    if (payload.empty()) return skin;

    skin.skinId = jsonExtractString(payload, "SkinId");

    string resourcePatchB64 = jsonExtractString(payload, "SkinResourcePatch");
    if (!resourcePatchB64.empty()) {
        vector<uint8_t> raw = base64Decode(resourcePatchB64);
        skin.skinResourcePatch = string(raw.begin(), raw.end());
    }

    skin.skinImageWidth  = (int32_t)jsonExtractInt(payload, "SkinImageWidth");
    skin.skinImageHeight = (int32_t)jsonExtractInt(payload, "SkinImageHeight");
    string skinDataB64 = jsonExtractString(payload, "SkinData");
    if (!skinDataB64.empty()) {
        skin.skinImageData = base64Decode(skinDataB64);
    }

    skin.capeId          = jsonExtractString(payload, "CapeId");
    skin.capeImageWidth  = (int32_t)jsonExtractInt(payload, "CapeImageWidth");
    skin.capeImageHeight = (int32_t)jsonExtractInt(payload, "CapeImageHeight");
    string capeDataB64 = jsonExtractString(payload, "CapeData");
    if (!capeDataB64.empty()) {
        skin.capeImageData = base64Decode(capeDataB64);
    }

    string geometryB64 = jsonExtractString(payload, "SkinGeometryData");
    if (!geometryB64.empty()) {
        vector<uint8_t> raw = base64Decode(geometryB64);
        skin.skinGeometryData = string(raw.begin(), raw.end());
    }
    // Real field name per gophertunnel/PMMP client-data schema; not present
    // in minecraft-data's steve.json template (that default skin predates
    // this field), so this stays empty for clients/tools that don't send
    // it -- matches this file's previous inert default.
    skin.skinGeometryDataVersion = jsonExtractString(payload, "SkinGeometryDataEngineVersion");

    string animDataB64 = jsonExtractString(payload, "SkinAnimationData");
    if (!animDataB64.empty()) {
        vector<uint8_t> raw = base64Decode(animDataB64);
        skin.skinAnimationData = string(raw.begin(), raw.end());
    }

    string armSizeStr = jsonExtractString(payload, "ArmSize");
    if (!armSizeStr.empty()) skin.armSize = armSizeStr;

    skin.personaSkin       = jsonExtractBool(payload, "PersonaSkin", false);
    skin.premiumSkin       = jsonExtractBool(payload, "PremiumSkin", false);
    skin.capeOnClassicSkin = jsonExtractBool(payload, "CapeOnClassicSkin", false);

    // Only treat this as a usable real skin once we actually have skin
    // pixel data AND dimensions that are internally consistent with it --
    // a client that sent no clientData at all (or one we failed to parse)
    // should fall back to the placeholder skin in PlayerListPacket.cpp,
    // not send a broken 0-byte "skin".
    skin.valid = !skin.skinImageData.empty()
              && skin.skinImageWidth > 0
              && skin.skinImageHeight > 0
              && (size_t)(skin.skinImageWidth * skin.skinImageHeight * 4) == skin.skinImageData.size();

    return skin;
}

void handleLogin(int sock, sockaddr_in clientAddr, ClientState& state,
                  const vector<uint8_t>& data, size_t offset) {
    if (offset + 4 > data.size()) {
        cout << "[Login] Packet too short for protocol version, dropping\n";
        return;
    }
    uint32_t protocolVersion = readUIntBE(data, offset);

    if (offset >= data.size()) {
        cout << "[Login] Missing connection request payload, dropping\n";
        return;
    }
    // Length of the chain+clientData block that follows. We don't need it
    // to know where each field is (both are individually length-prefixed),
    // just informationally.
    readVarInt(data, offset);

    if (offset >= data.size()) {
        cout << "[Login] Missing chain data, dropping\n";
        return;
    }
    string chainJson = readLPStringLE(data, offset);

    // Client data JWT (skin, device info, etc.) follows the chain, same
    // 4-byte-LE length prefix as the chain field above. Unlike the chain
    // (a JSON array of JWTs), this is a single raw JWT string -- its
    // payload holds the skin/appearance fields directly at the top level.
    string clientDataJwt;
    if (offset < data.size()) {
        clientDataJwt = readLPStringLE(data, offset);
    }

    LoginIdentity identity = extractLoginIdentity(chainJson);
    ClientSkinData skin = extractClientSkinData(decodeJwtPayload(clientDataJwt));

    cout << "===========================================\n";
    cout << "Login Packet\n";
    cout << "===========================================\n";
    cout << "  Protocol Version: " << protocolVersion << "\n";
    if (identity.valid) {
        cout << "  Display Name:     " << identity.displayName << "\n";
        cout << "  XUID:             " << identity.xuid << "\n";
        cout << "  Identity (UUID):  " << identity.identityUUID << "\n";
        cout << "  Xbox Authenticated: " << (identity.xboxAuthenticated ? "yes (chain verified against Mojang's root key)" : "no (self-signed / unverified chain)") << "\n";
    } else {
        cout << "  [!] Could not extract identity from certificate chain\n";
    }
    if (skin.valid) {
        cout << "  Skin:              " << skin.skinImageWidth << "x" << skin.skinImageHeight
             << " (" << (skin.personaSkin ? "persona" : "classic") << ")\n";
    } else {
        cout << "  [!] No usable skin in client data -- PlayerList will use the placeholder skin\n";
    }
    cout << "===========================================\n";

    state.loggedIn      = identity.valid;
    state.displayName    = identity.displayName;
    state.xuid           = identity.xuid;
    state.identityUUID   = identity.identityUUID;

    state.hasSkinData = skin.valid;
    if (skin.valid) {
        state.skinId                 = skin.skinId;
        state.skinResourcePatch      = skin.skinResourcePatch;
        state.skinImageWidth         = skin.skinImageWidth;
        state.skinImageHeight        = skin.skinImageHeight;
        state.skinImageData          = skin.skinImageData;
        state.capeId                 = skin.capeId;
        state.capeImageWidth         = skin.capeImageWidth;
        state.capeImageHeight        = skin.capeImageHeight;
        state.capeImageData          = skin.capeImageData;
        state.skinGeometryData       = skin.skinGeometryData;
        state.skinGeometryDataVersion = skin.skinGeometryDataVersion;
        state.skinAnimationData      = skin.skinAnimationData;
        state.armSize                = skin.armSize;
        state.personaSkin            = skin.personaSkin;
        state.premiumSkin            = skin.premiumSkin;
        state.capeOnClassicSkin      = skin.capeOnClassicSkin;
    }

    if (identity.valid && !identity.publicKeyBase64.empty()) {
        // A real Xbox Live login — e.g. the official Minecraft client,
        // signed in and playing online — always includes
        // "identityPublicKey" and expects the encryption handshake here.
        // Skipping straight to PlayStatus/ResourcePacksInfo (like the
        // offline flow below does) is why an official client would just
        // sit there after Login instead of continuing.
        vector<uint8_t> clientPublicKeyDer = base64Decode(identity.publicKeyBase64);
        cout << "[DEBUG] publicKeyBase64 field used: " << (jsonExtractString(chainJson, "identityPublicKey").empty() ? "cpk (fallback)" : "identityPublicKey") << "\n";
        cout << "[DEBUG] publicKeyBase64 value: " << identity.publicKeyBase64 << "\n";
        cout << "[DEBUG] clientPublicKeyDer length: " << clientPublicKeyDer.size() << " bytes\n";
        sendServerToClientHandshake(sock, clientAddr, state, clientPublicKeyDer);
        return;
    }

    // Older/offline-style logins with no identityPublicKey: keep the
    // previous unencrypted flow so local/offline testing still works.
    //
    // Per the official Bedrock login sequence (minecraft.wiki's "Bedrock
    // Login Sequence" page): ... -> ClientToServerHandshake ->
    // PlayStatus(LOGIN_SUCCESS) -> (Resource Pack Handling) ->
    // ResourcePacksInfo -> ... PlayStatus(LoginSuccess) is the packet that
    // closes out the Login stage and must go out BEFORE ResourcePacksInfo,
    // not after the resource pack stage completes.
    sendPlayStatus(sock, clientAddr, state, PlayStatusType::LoginSuccess);
    sendResourcePacksInfo(sock, clientAddr, state);
}
