#include "PlayerListPacket.h"
#include "DataTypeHelper.h"
#include "GamePacketSender.h"
#include <iostream>
#include <cstdint>
#include <cstdio>

using namespace std;

// Parses a standard "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" UUID string into
// its raw 16 bytes, in the exact order they appear in the dashed string
// (byte 0 = first hex pair, byte 15 = last hex pair) -- NO reversal, NO
// splitting into 64-bit halves.
//
// Verified against bedrock-protocol's actual "uuid" datatype (it's a
// "native" type, not a protodef-declared one): src/datatypes/minecraft.js
// writeUUID() does `UUID.parse(value).copy(buffer, offset)` -- a straight
// 16-byte buffer copy of the standard RFC-4122 byte layout. There is no
// 64-bit-halves/endianness step anywhere in that path.
//
// This file's previous version built two uint64 halves and wrote each via
// writeUInt64 (little-endian), which silently byte-reverses each 8-byte
// half. That was wrong and would have corrupted the uuid field -- the very
// first field after type/legacy_type in the "add" record -- desyncing
// every field that follows it in the packet. Confirmed correct via
// protocol.json (1.26.40) + protodef + bedrock-protocol source, not
// guessed.
// Falls back to all-zero bytes on any parse failure rather than sending garbage.
static void parseUUIDToBytes(const string& uuidStr, uint8_t out[16]) {
    for (int i = 0; i < 16; i++) out[i] = 0;
    string hex;
    hex.reserve(32);
    for (char c : uuidStr) {
        if (c != '-') hex.push_back(c);
    }
    if (hex.size() != 32) return; // malformed -- leave as zero rather than guess

    auto hexNibble = [](char c) -> uint8_t {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
    };
    for (int i = 0; i < 16; i++) {
        out[i] = (hexNibble(hex[i * 2]) << 4) | hexNibble(hex[i * 2 + 1]);
    }
}

// Writes a SkinImage: width(li32), height(li32), data(ByteArray = varint
// length + raw bytes). Confirmed against protocol.json's SkinImage type.
static void writeSkinImage(vector<uint8_t>& buf, int32_t width, int32_t height, const vector<uint8_t>& data) {
    writeInt(buf, width);
    writeInt(buf, height);
    writeVarInt(buf, (uint32_t)data.size());
    buf.insert(buf.end(), data.begin(), data.end());
}

// Queues a PlayerList "add" record describing `subject` onto `receiver`'s
// batch. receiver==subject is the original single-player case (own entry,
// unique id 1); for other players entityUniqueId must equal the id used in
// AddPlayer/MovePlayer for that player (ClientState::entityId).
void sendPlayerListAddOf(ClientState& receiver, ClientState& subject, int64_t entityUniqueId) {
    // Gamepacket header (varint) for PlayerList = 63 (0x3F).
    vector<uint8_t> packet;
    writeVarInt(packet, 63);

    // records: PlayerRecords -- a varint-count array of PlayerRecord.
    // Sending exactly one entry (the connecting player's own identity),
    // matching the real Dragonfly capture this was built from.
    writeVarInt(packet, 1); // records count = 1

    // PlayerRecord.type: mapper varint, 1 = "add"
    writeVarInt(packet, 1);
    // PlayerRecord.legacy_type: u8 -- a real Dragonfly capture shows this
    // as 0 for an "add" record, NOT the same value as the mapper "type"
    // field above (that was this file's original assumption, and wrong --
    // legacy_type has its own, older 2-value enum, not a mirror of type).
    writeUByte(packet, 0);

    // ---- "add" case fields (PlayerRecord's switch on type) ----
    uint8_t uuidBytes[16];
    parseUUIDToBytes(subject.identityUUID, uuidBytes);
    packet.insert(packet.end(), uuidBytes, uuidBytes + 16);

    writeZigZag64(packet, entityUniqueId); // entity_unique_id -- matches StartGame's entity_id/runtime_entity_id (both 1) for this single-player connection

    writeString(packet, subject.displayName);
    // xbox_user_id: a real capture shows the literal string "0" here, not
    // an empty string, even for a connection with no real XUID -- match
    // that instead of forwarding an empty subject.xuid.
    writeString(packet, subject.xuid.empty() ? "0" : subject.xuid);
    writeString(packet, ""); // platform_chat_id -- none

    // build_platform: li32 -- a real capture shows -1 (the documented
    // "unknown/unspecified" sentinel) here, not a real platform id. This
    // file previously hardcoded 1 ("Android") as a guess; -1 matches what
    // an actual working server sent.
    writeInt(packet, -1);

    // ---- Skin ----
    // Uses the real skin the connecting client sent in its Login packet's
    // client-data JWT (see extractClientSkinData() in LoginPacket.cpp)
    // whenever we have one. Falls back to the old baked placeholder --
    // a 64x64 flat skin-tone texture + built-in humanoid geometry -- only
    // for connections that sent no usable clientData (subject.hasSkinData
    // false), so offline/test logins still get a spec-valid skin instead
    // of an empty/invalid one.
    string skinId;
    string skinResourcePack;
    int32_t skinW, skinH;
    const vector<uint8_t>* skinPixelsPtr;
    vector<uint8_t> placeholderPixels; // only populated in the fallback branch
    int32_t capeW, capeH;
    const vector<uint8_t>* capePixelsPtr;
    string geometryData;
    string geometryDataVersion;
    string animationData;
    string capeId;
    uint8_t armSizeByte;
    bool personaSkin, premiumSkin, capeOnClassicSkin;

    if (subject.hasSkinData) {
        skinId              = subject.skinId;
        skinResourcePack    = subject.skinResourcePatch;
        skinW               = subject.skinImageWidth;
        skinH               = subject.skinImageHeight;
        skinPixelsPtr       = &subject.skinImageData;
        capeW               = subject.capeImageWidth;
        capeH               = subject.capeImageHeight;
        capePixelsPtr       = &subject.capeImageData;
        geometryData        = subject.skinGeometryData;
        geometryDataVersion = subject.skinGeometryDataVersion;
        animationData       = subject.skinAnimationData;
        capeId              = subject.capeId;
        // ArmSize arrives as the string "wide"/"slim" (client-data JSON);
        // the wire field is a mapper over u8 (0=slim, 1=wide) -- confirmed
        // against protocol.json earlier in this project. Default to wide
        // if the client sent something unexpected rather than guess.
        armSizeByte         = (subject.armSize == "slim") ? 0 : 1;
        personaSkin         = subject.personaSkin;
        premiumSkin         = subject.premiumSkin;
        capeOnClassicSkin   = subject.capeOnClassicSkin;
    } else {
        skinId = "Standard_Custom";
        skinResourcePack = "";
        // 64x64 is the smallest real valid skin preset size (64x32, 64x64,
        // 128x128, 256x256, 256x512, 512x256, 512x512) -- a client silently
        // hanging on a size that satisfies width*height*4==data.size() but
        // ISN'T one of those presets was this file's original spawn-hang bug.
        skinW = 64; skinH = 64;
        placeholderPixels.assign(skinW * skinH * 4, 0);
        for (size_t i = 0; i < placeholderPixels.size(); i += 4) {
            placeholderPixels[i + 0] = 198; // R
            placeholderPixels[i + 1] = 134; // G
            placeholderPixels[i + 2] = 66;  // B
            placeholderPixels[i + 3] = 255; // A -- fully opaque
        }
        skinPixelsPtr = &placeholderPixels;
        capeW = 0; capeH = 0;
        capePixelsPtr = nullptr; // writeSkinImage below treats this as empty
        // Referencing the built-in default humanoid model instead of
        // embedding a full custom geometry blob -- the same minimal-JSON
        // convention other custom server implementations (Nukkit/PMMP-style)
        // use for this exact purpose.
        geometryData = "{\"geometry\":{\"default\":\"geometry.humanoid.custom\"}}";
        geometryDataVersion = "";
        animationData = "";
        capeId = "";
        armSizeByte = 1; // "wide"
        personaSkin = false;
        premiumSkin = false;
        capeOnClassicSkin = false;
    }

    static const vector<uint8_t> kEmptyBuf;
    writeString(packet, skinId);               // skin_id
    writeString(packet, "");                   // play_fab_id -- not sent in bedrock-protocol's ClientData template; left empty
    writeString(packet, skinResourcePack);      // skin_resource_pack

    writeSkinImage(packet, skinW, skinH, *skinPixelsPtr);

    writeVarInt(packet, 0); // animations: array count = 0 -- AnimatedImageData isn't parsed yet (no JSON array
                             // parser in this project); spec-valid to leave empty, see LoginPacket.h note.

    writeSkinImage(packet, capeW, capeH, capePixelsPtr ? *capePixelsPtr : kEmptyBuf);

    writeString(packet, geometryData);
    writeString(packet, geometryDataVersion);
    writeString(packet, animationData);
    writeString(packet, capeId);
    writeString(packet, skinId); // full_skin_id -- reusing skin_id; this project doesn't track a
                                  // separate full/composite skin identifier

    writeUByte(packet, armSizeByte); // arm_size: mapper u8, 0=slim/1=wide (verified against protocol.json --
                                       // it's a mapper over u8, not a raw string)
    writeIntBE(packet, 0); // skin_color: type is "i32" (protocol.json), which protodef's numeric.js maps to
                            // writeInt32BE -- big-endian. Left inert (0) rather than parsed from the
                            // client's "#rrggbb" SkinColor string: a real Dragonfly capture of a persona
                            // skin also sent 0 here, suggesting this field isn't render-relevant for the
                            // skins we're forwarding -- revisit if that turns out wrong.

    writeVarInt(packet, 0); // personal_pieces: array count = 0 -- PersonaPieces isn't parsed yet (same
                             // no-array-parser reason as animations above)
    writeVarInt(packet, 0); // piece_tint_colors: array count = 0 -- same reason

    writeBool(packet, premiumSkin);
    writeBool(packet, personaSkin);
    writeBool(packet, capeOnClassicSkin);
    writeBool(packet, true);  // primary_user -- sole account on this connection, not split-screen
    writeBool(packet, true);  // overriding_player_appearance -- this skin should be applied

    writeString(packet, ""); // trusted -- no signature/verification concept implemented yet
    writeString(packet, ""); // profile_hash
    // ---- end Skin ----

    writeBool(packet, false); // is_teacher
    writeBool(packet, &receiver == &subject);  // is_host -- sole player on this server right now
    writeBool(packet, false); // is_subclient
    writeIntBE(packet, 0);    // player_color: type is "i32" (big-endian) -- same fix as skin_color above

    queueGamePacket(receiver, packet);
    cout << "[Bedrock] Sent PlayerList(add, username=" << subject.displayName << ")\n";
}

void sendPlayerListAdd(int sock, sockaddr_in clientAddr, ClientState& state) {
    (void)sock; (void)clientAddr;
    sendPlayerListAddOf(state, state, 1);
}

// Same 16 UUID bytes PlayerList uses, so AddPlayer/PlayerList(remove) match it.
void writePlayerUuid(vector<uint8_t>& buf, const string& uuidStr) {
    uint8_t b[16];
    parseUUIDToBytes(uuidStr, b);
    buf.insert(buf.end(), b, b + 16);
}

// PlayerList "remove" record (gophertunnel PlayerListEntry.Marshal: variant
// varuint 0, legacy action u8 1, then just the UUID).
void sendPlayerListRemove(ClientState& receiver, ClientState& subject) {
    vector<uint8_t> packet;
    writeVarInt(packet, 63);
    writeVarInt(packet, 1); // records count
    writeVarInt(packet, 0); // variant: remove
    writeUByte(packet, 1);  // legacy action: remove
    writePlayerUuid(packet, subject.identityUUID);
    queueGamePacket(receiver, packet);
}

