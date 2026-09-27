#include "StartGamePacket.h"
#include "DataTypeHelper.h"
#include "GamePacketSender.h"
#include "ItemRegistryPacket.h"
#include "AvailableActorIdentifiersPacket.h"
#include "BiomeDefinitionListPacket.h"
#include "CreativeContentPacket.h"
#include "CraftingDataPacket.h"
#include "SetSpawnPositionPacket.h"
#include "UpdateAttributesPacket.h"
#include <iostream>

using namespace std;

namespace {

// Player Property Data (field 17) and a ServerBlockProperty's Block
// Definition are both documented as type "unknown" — real Bedrock traffic
// carries these as little-endian NBT. This project has no NBT
// reader/writer, and doesn't need one yet: Block Properties is sent as an
// empty array below (no custom blocks), and Player Property Data has
// nothing real to report, so both just need *a* validly-shaped empty NBT
// value rather than a full NBT library. This writes the smallest valid
// one: an empty TAG_Compound (type 0x0A, zero-length name, immediately
// closed by TAG_End).
void writeEmptyNbtCompound(vector<uint8_t>& buf) {
    // CONFIRMED WRONG and fixed: Bedrock's Network-Little-Endian NBT (the
    // variant used over the wire, per wiki.bedrock.dev's NBT-in-depth page)
    // length-prefixes every tag name and string with a VarInt, NOT a 2-byte
    // ushort like the on-disk Little-Endian NBT format uses. Writing
    // writeUShort(buf, 0) here put an extra stray zero byte after this
    // field, shifting everything that follows StartGame's Player Property
    // Data (ServerBlockStateChecksum, WorldTemplateID,
    // BlockNetworkIdsAreHashes, ...) by one byte. Confirmed directly
    // against a real client capture: BlockNetworkIdsAreHashes decoded as
    // false instead of the true this project actually sends, exactly the
    // byte-shift this bug predicts. The very same bug, baked into every one
    // of ItemRegistryData.cpp's 1934 items instead of just once, is what
    // makes ItemRegistry entirely undecodable and is why a real client
    // disconnects right after StartGame+ItemRegistry -- see that file.
    writeUByte(buf, 0x0A);  // TAG_Compound
    writeVarInt(buf, 0);    // name length = 0, VarInt-encoded (1 byte)
    writeUByte(buf, 0x00);  // TAG_End — closes the compound immediately
}

} // namespace

void sendStartGame(int sock, sockaddr_in clientAddr, ClientState& state) {
    // REMOVED: a previous pass sent a fabricated "VoxelShapesPacket" here,
    // right before StartGame -- in the same combined batch (see
    // GamePacketSender.cpp's flushGamePackets(), which sends everything
    // queued between here and ItemRegistry as one unit). Checked
    // gophertunnel's actual complete packet registry (pool.go) directly:
    // there is no IDVoxelShapes, no VoxelShapes packet at all, anywhere in
    // the real protocol. "Voxel Shapes" is a real Bedrock concept, but it's
    // resource-pack CONTENT (block face-culling geometry, added in the
    // 26.30 changelog / minecraft-creator docs) -- not a network packet.
    // Sending an unrecognized packet ID as literally the first thing in
    // the same batch as StartGame+ItemRegistry would corrupt the client's
    // parse of that entire batch from byte one, before it ever reaches
    // StartGame's real content -- independent of how correct StartGame
    // and ItemRegistry's own encoders are, which is exactly why
    // exhaustively verifying those two never found the actual bug.
    // Confirmed against real client behavior too: after ResourcePackStack
    // completes, the client's own next expected packet is StartGame
    // directly, nothing else first.

    // Gamepacket header (varint) for StartGame = 11 (0x0B).
    vector<uint8_t> packet;
    writeVarInt(packet, 0x0B);

    // ── 0: Entity ID (ActorUniqueID, varint64/zigzag64) ────────────────
    // ── 1: Runtime ID (ActorRuntimeID, varuint64) ───────────────────────
    // This project has exactly one player per connection, so both ids are
    // just fixed at 1 (0 is reserved/commonly read as "no entity" by
    // clients) rather than tracked per-client state that doesn't exist yet.
    writeZigZag64(packet, 1); // ActorUniqueID
    writeVarInt64(packet, 1); // ActorRuntimeID

    // ── 2: Game Type (varint32/zigzag) ──────────────────────────────────
    writeZigZag32(packet, static_cast<int32_t>(GameType::Survival));

    // ── 3: Position (Vec3: float x, y, z) ───────────────────────────────
    // Y=4.0: standing on top of the grass surface at world Y=3 (see
    // LevelChunkPacket.cpp for the actual terrain layout).
    writeFloat(packet, 0.0f);
    writeFloat(packet, 4.0f);
    writeFloat(packet, 0.0f);

    // ── 4: Rotation (Vec2: float x, y) ──────────────────────────────────
    writeFloat(packet, 0.0f);
    writeFloat(packet, 0.0f);

    // ── 5: Settings (LevelSettings) ─────────────────────────────────────
    {
        writeUInt64(packet, 0); // 0: Seed

        // 1: Spawn Settings (SpawnSettings)
        writeShort(packet, static_cast<int16_t>(SpawnBiomeType::Default)); // 0: Spawn Biome Type
        writeString(packet, "");                                          // 1: User Defined Biome Name
        writeZigZag32(packet, 0);                                         // 2: Dimension (0 = Overworld)

        writeZigZag32(packet, static_cast<int32_t>(GeneratorType::Overworld)); // 2: Generator Type
        writeZigZag32(packet, static_cast<int32_t>(GameType::Survival));      // 3: Game Type
        writeBool(packet, false);                                             // 4: Is Hardcore
        writeZigZag32(packet, 2);   // 5: Game Difficulty (2 = Normal, as the gophertunnel/Dragonfly reference sends)

        // 6: Default Spawn Block Position — RE-CONFIRMED by fetching the
        // complete, current start_game.go directly (raw source, not a
        // paraphrased snippet this time): `io.BlockPos(&pk.WorldSpawn)`.
        // BlockPos IS the right function after all -- all three axes
        // zigzag/signed. A previous pass (this one, working off an
        // incomplete search snippet) wrongly "corrected" this to
        // UBlockPos/unsigned-Y. Reverted back to all-zigzag, which is what
        // this project actually had originally.
        writeZigZag32(packet, 0);
        writeZigZag32(packet, 4);
        writeZigZag32(packet, 0);

        writeBool(packet, true);  // 7: Achievements Disabled (reference sends true)
        writeZigZag32(packet, static_cast<int32_t>(EditorWorldType::NonEditor)); // 8: Editor World Type
        writeBool(packet, false); // 9: Is Created In Editor
        writeBool(packet, false); // 10: Is Exported From Editor
        writeZigZag32(packet, 0); // 11: Day Cycle Stop Time (disabled — 0)

        writeVarInt(packet, static_cast<uint32_t>(EducationEditionOffer::None)); // 12: Education Edition Offer
        writeBool(packet, true);   // 13: Education Features Enabled (reference sends true)
        writeString(packet, "");   // 14: Education Product ID

        writeFloat(packet, 0.0f); // 15: Rain Level
        writeFloat(packet, 0.0f); // 16: Lightning Level

        writeBool(packet, false); // 17: Has Confirmed Platform Locked Content
        writeBool(packet, true);  // 18: Multiplayer Game Intent
        writeBool(packet, true);  // 19: LAN Broadcast Intent (reference sends true)

        writeVarInt(packet, static_cast<uint32_t>(BroadcastSetting::Public)); // 20: Xbox Live Broadcast Setting -- plain varint per the real protocol.json (protocol 2168), NOT zigzag32. Confirmed directly against the schema this time, not gophertunnel's Go source.
        writeVarInt(packet, static_cast<uint32_t>(BroadcastSetting::Public)); // 21: Platform Broadcast Setting -- same correction

        writeBool(packet, true);  // 22: Commands Enabled
        writeBool(packet, false); // 23: Texture Packs Required

        // 24: Rule Data (GameRulesChangedPacketData)
        writeVarInt(packet, 0); // Rules List: array count = 0 (no gamerule
                                 // overrides — the oneOf-typed rule value
                                 // encoding isn't implemented since there's
                                 // nothing to send yet)

        // 25: Experiments (Experiments) — confirmed against gophertunnel's
        // start_game.go: `protocol.SliceUint32Length(io, &pk.Experiments)`.
        // That's a plain 4-byte length prefix, unlike the VarInt-prefixed
        // slices used everywhere else in this packet (e.g. GameRules just
        // above, or Block Properties below) — writing this one as a VarInt
        // undercounts by 3 bytes for an empty list and shifts every field
        // that follows.
        writeInt(packet, 0);      // Toggles: array count = 0 (LE int32, not VarInt)
        writeBool(packet, false); // ExperimentsEverToggled

        writeBool(packet, false); // 26: Has Bonus Chest Enabled
        writeBool(packet, false); // 27: Start With Map Enabled

        writeZigZag32(packet, static_cast<int32_t>(PlayerPermissionLevel::Member)); // 28: Player Permissions (Varint32/zigzag, not a raw byte)

        writeInt(packet, 0); // 29: Server Chunk Tick Range (fixed LE int32)

        writeBool(packet, false); // 30: Has Locked Behavior Pack
        writeBool(packet, false); // 31: Has Locked Resource Pack
        writeBool(packet, false); // 32: Is From Locked Template
        writeBool(packet, false); // 33: Use Msa Gamertags Only
        writeBool(packet, false); // 34: Is From World Template
        writeBool(packet, false); // 35: Is World Template Option Locked
        writeBool(packet, false); // 36: Only Spawn V1 Villagers
        writeBool(packet, false); // 37: Persona Disabled
        writeBool(packet, false); // 38: Custom Skins Disabled
        writeBool(packet, false); // 39: Emote Chat Muted

        writeString(packet, "1.26.51"); // 40: Base Game Version — must match the
                                     // client's actual build (protocol 2193)
                                     // and agree with ResourcePacksStack's copy

        writeInt(packet, 0); // 41: Limited World Width (fixed LE int32)
        writeInt(packet, 0); // 42: Limited World Depth (fixed LE int32)

        writeBool(packet, false); // 43: Nether Type

        // 44: Edu Shared Uri Resource (EduSharedUriResource)
        writeString(packet, ""); // Button Name
        writeString(packet, ""); // Link Uri

        // 45: Override Force Experimental Gameplay — the doc doesn't mark
        // this "(Required)" like every neighboring field, which matches
        // the known Bedrock convention for an Optional<T>: a presence
        // bool, then the value only if present. Nothing to override, so
        // it's sent as not-present.
        writeBool(packet, false);

        writeUByte(packet, static_cast<uint8_t>(ChatRestrictionLevel::None)); // 46: Chat Restriction Level
        writeBool(packet, false); // 47: Disable Player Interactions

        // RESTORED, with direct evidence this time: grepped the actual
        // protocol.json (bedrock 1.26.40, protocol 2168) rather than
        // relying on gophertunnel's Go source, and both of these fields
        // are genuinely there right after disable_player_interactions:
        //   server_editor_connection_policy: zigzag32
        //   allow_anonymous_block_drops_in_editor_worlds: bool
        // The previous removal (based on gophertunnel not seeming to have
        // them) was wrong -- gophertunnel was likely just behind this
        // specific protocol revision. Missing these 2 fields shifted
        // every byte after them for the rest of the packet, which lines
        // up with a downstream li64 field eventually running out of
        // buffer entirely.
        writeZigZag32(packet, 0); // 48: Server Editor Connection Policy
        writeBool(packet, false); // 49: Allow Anonymous Block Drops In Editor Worlds
    }

    writeString(packet, "world");     // 6: Level ID
    writeString(packet, "Bedrock LB World"); // 7: Level Name
    writeString(packet, "");          // 8: Template Content Identity
    writeBool(packet, false);         // 9: Is Trial

    // 10: Movement Settings (SyncedPlayerMovementSettings)
    writeZigZag32(packet, 0);         // Rewind History Size (0 = client-authoritative, no server rewind)
    writeBool(packet, true);          // Server Authoritative Block Breaking (reference: true)

    writeUInt64(packet, 0);           // 11: Level Current Time
    writeZigZag32(packet, 0);         // 12: Enchantment Seed

    writeVarInt(packet, 0);           // 13: Block Properties: array count = 0 (no custom blocks)

    writeString(packet, "00000000-0000-0000-0000-000000000000"); // 14: Multiplayer Correlation Id
    // 15: Enable Item Stack Net Manager == "server authoritative inventory".
    // The working reference (Dragonfly) sends TRUE. This server used to send
    // false while also sending modern full-size inventory windows, i.e. it
    // told the client to use the legacy inventory system. Aligned with the
    // reference; nothing in this server depends on the legacy behaviour.
    writeBool(packet, true);
    // 16: GameVersion — this is a real version-compatibility field a
    // client can validate against its own build, not a free-text server
    // name. Was sending the literal string "LBBNet" here, which doesn't
    // parse as a version at all. Matched to BaseGameVersion (field 40,
    // "1.26.51") and to the protocol=2193 this project's own log confirms
    // the connecting client actually declares in RequestNetworkSettings.
    writeString(packet, "1.26.51"); // 16: Game Version

    writeEmptyNbtCompound(packet);    // 17: Player Property Data (empty NBT compound — see helper above)

    writeUInt64(packet, 0);           // 18: Server Block Type Registry Checksum

    writeUInt64(packet, 0);           // 19: World Template ID (mce::UUID) — most significant bits
    writeUInt64(packet, 0);           //     least significant bits

    writeBool(packet, false);         // 20: Server Enabled ClientSide Generation
    // 21: BlockNetworkIds Are Hashes -- MUST be true now that LevelChunk
    // sends real (non-air) blocks. Without this, the palette entries in
    // LevelChunkPacket would need to be exact integer indices into this
    // specific client's compiled-in block table, which this project has
    // no copy of. With it, palette entries are stable per-block hashes
    // (see BlockHash.h) the client can look up by content instead.
    writeBool(packet, true);

    writeBool(packet, false);         // 22: ServerAuthoritativeSound (real, confirmed last "plain" field)

    // ServerJoinInformation is a real field: protocol.Optional[T], written
    // via OptionalMarshaler -- a presence bool, then the struct's own
    // fields only if present. Confirmed by fetching the complete raw
    // start_game.go directly. Nothing to report, so not-present.
    writeBool(packet, false); // ServerJoinInformation: not present

    // ServerID, ScenarioID, WorldID, OwnerID (telemetry strings) — four
    // plain strings, confirmed to genuinely belong at the very end of the
    // packet, in this exact order. (A previous pass moved these earlier
    // in the packet based on a paraphrased/incomplete search snippet;
    // that was wrong and has been reverted -- this was their correct
    // position all along.)
    writeString(packet, ""); // Server Id
    writeString(packet, ""); // Scenario Id
    writeString(packet, ""); // World Id
    writeString(packet, ""); // Owner Id

    queueGamePacket(state, packet);
    cout << "[Bedrock] Sent StartGame\n";

    // Confirmed against gophertunnel's real Conn: WritePacket() only
    // buffers, and Flush() is what combines everything queued since the
    // last flush into ONE encrypted batch. StartGame and ItemRegistry are
    // meant to go out together in a single combined batch, not as two
    // separate encrypted RakNet messages -- see queueGamePacket() is used
    // here and flushGamePackets() is called once, after ItemRegistry, not
    // after each individually.
    sendItemRegistry(sock, clientAddr, state);
    flushGamePackets(sock, clientAddr, state);
}
