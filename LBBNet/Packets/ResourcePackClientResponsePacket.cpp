#include "ResourcePackClientResponsePacket.h"
#include "Multiplayer.h"
#include "DataTypeHelper.h"
#include "GamePacketSender.h"
#include "ResourcePackStackPacket.h"
#include "StartGamePacket.h"
#include "JigsawStructureDataPacket.h"
#include "VoxelShapesPacket.h"
#include "LevelChunkPacket.h"
#include "NetworkChunkPublisherUpdatePacket.h"
#include "PlayStatusPacket.h"
#include "InventoryContentPacket.h"
#include "AvailableActorIdentifiersPacket.h"
#include "BiomeDefinitionListPacket.h"
#include "CreativeContentPacket.h"
#include "CraftingDataPacket.h"
#include "SetSpawnPositionPacket.h"
#include "UpdateAttributesPacket.h"
#include "PlayerListPacket.h"
#include "TrimDataPacket.h"
#include "SetTimePacket.h"
#include "GameRulesChangedPacket.h"
#include "LevelEventPacket.h"
#include "SetPlayerGameTypePacket.h"
#include "UpdatePlayerGameTypePacket.h"
#include "UpdateAbilitiesPacket.h"
#include "SetEntityDataPacket.h"
#include "TextPacket.h"
#include "AvailableCommandsPacket.h"
#include "ItemRegistryPacket.h"
#include <iostream>

using namespace std;

void handleResourcePackClientResponse(int sock, sockaddr_in clientAddr, ClientState& state,
                                       const vector<uint8_t>& data, size_t offset) {
    if (offset >= data.size()) {
        cout << "[ResourcePackClientResponse] Packet too short for status, dropping\n";
        return;
    }
    auto status = static_cast<ResourcePackResponseStatus>(readUByte(data, offset));

    // Only present when status == SendPacks: a list of pack ids the client
    // wants downloaded. We never offer any packs, so this should never
    // actually happen — but a real client could still send an empty list
    // here, so read (and discard) it defensively to keep offset sane for
    // anyone parsing further, rather than assuming the field is absent.
    if (status == ResourcePackResponseStatus::SendPacks && offset + 2 <= data.size()) {
        uint16_t packCount = readUShort(data, offset);
        for (uint16_t i = 0; i < packCount && offset < data.size(); i++) {
            readString(data, offset);
        }
    }

    switch (status) {
        case ResourcePackResponseStatus::Refused:
            cout << "[ResourcePackClientResponse] Refused — disconnecting isn't implemented "
                    "yet, but the client has declined to continue\n";
            break;

        case ResourcePackResponseStatus::SendPacks:
            // We have no packs to send; nothing to do but wait for the
            // client to re-check and report HaveAllPacks.
            cout << "[ResourcePackClientResponse] SendPacks requested, but no packs are served\n";
            break;

        case ResourcePackResponseStatus::HaveAllPacks:
            cout << "[ResourcePackClientResponse] HaveAllPacks\n";
            // Guard against resending ResourcePacksStack if the resource
            // pack stage (and everything after it — StartGame,
            // ItemRegistry, ...) has already completed. A real client can
            // apparently send a second HaveAllPacks after already
            // receiving StartGame+ItemRegistry (confirmed: this isn't a
            // frame-ordering artifact — the Reliable-Ordered delivery fix
            // in PacketHandler.cpp still shows this as a genuine second,
            // later message, not a reordered one). Resending
            // ResourcePacksStack at that point — telling an already-mid-
            // spawn client to go back to resource-pack negotiation — is
            // protocol-nonsensical and could plausibly be what confuses a
            // real client into giving up. Once startGameSent is true,
            // there's nothing useful to do with a stray HaveAllPacks;
            // just acknowledge it in the log and otherwise ignore it.
            if (state.startGameSent) {
                cout << "[ResourcePackClientResponse] Stage already complete "
                        "(StartGame already sent) — ignoring stray HaveAllPacks, "
                        "NOT resending ResourcePacksStack\n";
                break;
            }
            sendResourcePacksStack(sock, clientAddr, state);
            break;

        case ResourcePackResponseStatus::Completed:
            // Resource pack stage is done per a real client response.
            cout << "[ResourcePackClientResponse] Completed — resource pack stage done\n";
            completeResourcePackStageAndStartGame(sock, clientAddr, state);
            break;
    }
}

void completeResourcePackStageAndStartGame(int sock, sockaddr_in clientAddr, ClientState& state) {
    // Guard against sending this twice — see the comment on
    // ClientState::startGameSent in Clients.h for why that's possible now.
    if (state.startGameSent) {
        cout << "[StartGame] Already sent for this client, skipping duplicate trigger\n";
        return;
    }
    state.startGameSent = true;

    // StartGame (+ ItemRegistry, sent from inside sendStartGame itself) is
    // ALL that happens here. A real Dragonfly capture (packet-logger_log.txt)
    // was checked directly, packet-by-packet, and confirmed that every
    // other definition/world/inventory packet this project sends
    // (AvailableActorIdentifiers, the real BiomeDefinitionList, CraftingData,
    // SetSpawnPosition, UpdateAttributes, chunks, NetworkChunkPublisherUpdate,
    // InventoryContent, ...) is actually sent AFTER PlayStatus(PlayerSpawn)
    // in the real sequence, not before it — an earlier pass of this file
    // had all of that here, which was wrong. See sendSpawnSequence() below
    // for the corrected, capture-verified order.
    // A real Dragonfly capture (packet-logger_log.txt) showed two empty
    // packets sent right before StartGame that this project was previously
    // missing entirely: JigsawStructureData then VoxelShapes, in that
    // order. An earlier pass here incorrectly believed VoxelShapes wasn't
    // a real packet and removed it -- the Dragonfly capture disproves
    // that; both are real, currently-used packet IDs (313 and 337) that a
    // working server sends at exactly this point in the handshake.
    cout << "[StartGame] Sending JigsawStructureData (empty)\n";
    sendJigsawStructureData(sock, clientAddr, state);

    cout << "[StartGame] Sending VoxelShapes (empty)\n";
    sendVoxelShapes(sock, clientAddr, state);

    cout << "[StartGame] Sending StartGame\n";
    sendStartGame(sock, clientAddr, state);
}

void sendSpawnSequence(int sock, sockaddr_in clientAddr, ClientState& state) {
    // Full post-RequestChunkRadius sequence, reconstructed by reading a
    // real Dragonfly server capture (packet-logger_log.txt) packet by
    // packet, not by guessing from gophertunnel's minimal built-in
    // handler (which only covers 4 of these — gophertunnel is a low-level
    // library; Dragonfly is a full game server built on top of it that
    // sends a lot more from its own application layer). Numbers below are
    // that capture's packet indices, for anyone diffing against it again:
    //   #7  ChunkRadiusUpdated        -- sent by the caller in PacketHandler.cpp
    //   #8  BiomeDefinitionList EMPTY
    //   #9  PlayStatus(PlayerSpawn)
    //   === SPAWNED ===
    //   #10 CreativeContent EMPTY
    //   #12 BiomeDefinitionList REAL (87 entries — #11 was a duplicate
    //        empty ItemRegistry, skipped, see note at the end of this
    //        function for everything intentionally left out)
    //   #14 CraftingData
    //   #15 TrimData
    //   #16 UpdateAttributes
    //   #21 SetTime
    //   #22 GameRulesChanged
    //   #23-24 LevelEvent (StopRain, StopThunder)
    //   #25 SetSpawnPosition
    //   #26 NetworkChunkPublisherUpdate
    //   #27 AvailableActorIdentifiers
    //   #28 SetPlayerGameType
    //   #29 UpdateAbilities
    //   #30 UpdatePlayerGameType
    //   #31 SetEntityData
    //   #32-35 InventoryContent (this project sends 3 windows, not 4 — see note)
    //   #36 Text (join message)
    //   #37 AvailableCommands EMPTY
    //   #39 LevelChunk (chunk streaming starts)
    if (state.spawnSequenceSent) {
        cout << "[Spawn] Already sent for this client, skipping duplicate trigger\n";
        return;
    }
    state.spawnSequenceSent = true;

    // NOTE: no longer sending an empty BiomeDefinitionList here. A real
    // Dragonfly capture against a modern (1.26.40) client via
    // bedrock-protocol shows it going straight from ChunkRadiusUpdated to
    // PlayStatus(PlayerSpawn) -- NO empty BiomeDefinitionList in between at
    // all. That empty packet exists purely for pre-1.21.80 client
    // compatibility (their achievement system assumes all biomes are
    // present); Dragonfly is version-aware enough to skip it for anything
    // newer, which this project wasn't. This project's target (1.26.45) is
    // well past that cutoff, so sending it unconditionally was itself the
    // deviation from a real server, not the fix.
    sendPlayStatus(sock, clientAddr, state, PlayStatusType::PlayerSpawn);
    sendCreativeContent(sock, clientAddr, state); // EMPTY here -- matches gophertunnel's handshake default exactly
    // -- Phase 1 ends here. -----------------------------------------------
    // The working reference (gophertunnel/Dragonfly) sends nothing but
    // PlayerSpawn and CreativeContent at this point and then WAITS for the
    // client's SetLocalPlayerAsInitialised before sending anything else
    // (Dragonfly's StartGame blocks on it). This server used to send ~40
    // more packets straight away, including a second full 1,934-item
    // ItemRegistry (the reference's second registry carries only custom
    // items, i.e. is empty), and the real client never sent
    // SetLocalPlayerAsInitialised -- it aborted somewhere inside that burst.
    // Splitting the sequence like the reference makes the failure point
    // observable and removes the duplicates: the second ItemRegistry and
    // second CreativeContent are no longer sent.
    cout << "[Spawn] Phase 1 sent (PlayerSpawn + CreativeContent); holding the rest until "
            "SetLocalPlayerAsInitialised (or the first PlayerAuthInput)\n";
    flushGamePackets(sock, clientAddr, state);
}

void sendSpawnSequencePhase2(int sock, sockaddr_in clientAddr, ClientState& state, const char* trigger) {
    if (state.spawnPhase2Sent) return;
    state.spawnPhase2Sent = true;

    // INTENTIONALLY DISABLED, for a deliberate isolation test.
    //
    // CORRECTION to earlier reasoning: I previously said SetLocalPlayerAsInitialised
    // requires the client to have terrain first. That was wrong, and this
    // project's own real gophertunnel source PROVES it's wrong: in
    // minecraft/conn.go, `conn.StartGame()`/`StartGameContext()` blocks
    // until it receives SetLocalPlayerAsInitialised FROM the client, and
    // only returns after that. Dragonfly's own chunk-sending code
    // (session.New() and Session.Spawn(), in server/session/session.go)
    // is only ever called AFTER conn.StartGameContext() returns --
    // meaning a real Dragonfly server sends ZERO chunks, ZERO inventory,
    // and ZERO biome/creative/crafting data before the client sends this
    // packet. Real Bedrock clients connecting to real Dragonfly servers
    // still send it anyway, every time. So a real client does NOT need
    // terrain to send SetLocalPlayerAsInitialised -- whatever it's
    // actually waiting on must be fully contained in the handshake
    // packets themselves (StartGame, ItemRegistry, ChunkRadiusUpdated,
    // PlayStatus, CreativeContent(empty)).
    //
    // So: this is a genuine, valid isolation test, not a broken one. If
    // this build still never receives SetLocalPlayerAsInitialised, the
    // bug is somewhere in those 7 handshake packets themselves -- not
    // "missing world data" (there isn't supposed to be any yet).
    cout << "[Spawn] Reached SetLocalPlayerAsInitialised, released by: " << trigger
         << ". Phase 2 is OFF for this test -- only the confirmed StartGame -> "
            "SetLocalPlayerAsInitialised handshake packets were sent before this point.\n";
    return;

    // Everything below is ordered and sourced directly against Dragonfly's
    // real Go source (server/server.go's finaliseConn, then
    // server/session/session.go's Config.New() and Session.Spawn()),
    // rather than a capture, so this section can be diffed against those
    // functions again in the future.

    // server.go: finaliseConn(), immediately after conn.StartGameContext()
    // returns (i.e. right after SetLocalPlayerAsInitialised is handled) --
    // a second ItemRegistry, empty (custom items only).
    sendEmptyItemRegistry(sock, clientAddr, state);

    // session.go: Config.New() -- session/world setup, in this exact order.
    sendBiomeDefinitionList(sock, clientAddr, state);   // sendBiomes() -- the REAL 87-entry one
    sendCreativeContentWithItems(sock, clientAddr, state); // CreativeContent, 2nd time, REAL items now
    sendCraftingData(sock, clientAddr, state);          // sendRecipes()
    sendEmptyTrimData(sock, clientAddr, state);         // sendArmourTrimData()
    sendUpdateAttributesSpeed(sock, clientAddr, state, 0.1f); // SendSpeed(0.1)

    // PlayerList(add) -- not part of Config.New()/Spawn() in the source
    // directly (that's a multiplayer broadcast mechanism elsewhere), but
    // kept here: a real Dragonfly capture showed a working server sending
    // it at this exact point, and it's needed for the client to resolve
    // its own skin/identity.
    sendPlayerListAdd(sock, clientAddr, state);

    // session.go: Spawn() -- in this exact order: SendHealth, then
    // SendExperience, then SendFood (3 DIFFERENT UpdateAttributes calls,
    // not repeats).
    sendUpdateAttributesHealth(sock, clientAddr, state, /*health*/20.0f, /*maxHealth*/20.0f, /*absorption*/0.0f);
    sendUpdateAttributesExperience(sock, clientAddr, state, /*level*/0, /*progress*/0.0f);
    sendUpdateAttributesFood(sock, clientAddr, state, /*food*/20, /*saturation*/0.0f, /*exhaustion*/0.0f);

    // REMOVED from here: SetTime, GameRulesChanged, LevelEvent(StopRain/
    // StopThunder), SetSpawnPosition, UpdatePlayerGameType. Checked
    // directly against dragonfly's source (grepped every writePacket call
    // in server/session/*.go): none of these are sent as part of the join
    // sequence at all. SetTime and UpdatePlayerGameType live in world.go
    // as separate runtime-triggered functions (called later, when time or
    // gamemode actually change); GameRulesChanged is a player.go function
    // for runtime rule changes, not join; SetSpawnPosition
    // (SendPlayerSpawn) is only sent when a spawn point is actually set
    // (e.g. sleeping in a bed). Gamerule *defaults* ride inside StartGame
    // itself (its own Rule Data field), which this project already sends
    // separately. None of these were wrong to have tried, but they aren't
    // what a real Dragonfly server sends here.

    // FIXED: this was 64 (blocks) = a 4-chunk radius, but the chunk-send
    // loop below only ever sends a 3x3 grid (1-chunk radius = 16 blocks)
    // around spawn. NetworkChunkPublisherUpdate is the server telling the
    // client "everything within this radius is available now" -- claiming
    // 64 while only actually publishing 16 worth of terrain meant a real
    // client would wait for chunks it was promised but that never arrive,
    // hanging on the loading screen until it gives up and disconnects.
    // NOTE: this alone turned out not to be enough -- ChunkRadiusUpdated
    // (sent much earlier, in PacketHandler.cpp) was STILL echoing the
    // client's full requested radius (e.g. 8) instead of being clamped to
    // this same number, so the client was told two different, contradictory
    // things about its view distance. Both are now driven off
    // kSentChunkRadius (see LevelChunkPacket.h) so they can't disagree
    // again. If the chunk-send loop below is ever widened to cover more
    // area, bump kSentChunkRadius and this stays in sync automatically.
    sendNetworkChunkPublisherUpdate(sock, clientAddr, state, 0, 4, 0, kSentChunkRadius * 16);
    sendAvailableActorIdentifiers(sock, clientAddr, state); // sendAvailableEntities()

    // session.go: SetGameMode() -- confirmed to call these two together,
    // in exactly this order (SetPlayerGameType, then UpdateAbilities via
    // SendAbilities). UpdatePlayerGameType is NOT part of this (see above).
    sendSetPlayerGameType(sock, clientAddr, state, 1); // creative, matches StartGame's player_gamemode
    sendUpdateAbilities(sock, clientAddr, state);

    // MobEffect would go here (once per active effect) -- skipped, this
    // project has no effects system yet, so there's nothing to send.

    sendSetEntityData(sock, clientAddr, state); // ViewEntityState()

    // sendInv() x4, in exactly this order (inventory, ui, offhand, armour).
    sendInventoryContent(sock, clientAddr, state, InventoryWindowId::Inventory);
    sendInventoryContent(sock, clientAddr, state, InventoryWindowId::UI);
    sendInventoryContent(sock, clientAddr, state, InventoryWindowId::OffHand);
    sendInventoryContent(sock, clientAddr, state, InventoryWindowId::Armor);

    sendJoinMessage(sock, clientAddr, state, state.displayName); // the join chat message
    // AvailableCommands: kept, even though it's not sent at this exact
    // point in the source (it's driven by a background/command-registry
    // mechanism elsewhere) -- sending an empty one here is harmless and
    // means a client asking for commands gets a well-formed (if empty)
    // answer instead of nothing.
    sendAvailableCommands(sock, clientAddr, state);

    // Chunk streaming itself starts here too in the real capture (#39),
    // not pre-spawn — send the spawn chunk plus its immediate neighbors
    // (real superflat terrain, see LevelChunkPacket.h) now. Bounded by
    // kSentChunkRadius, same constant ChunkRadiusUpdated and
    // NetworkChunkPublisherUpdate were clamped to above -- widen that one
    // constant to send more than a 3x3 patch, don't just change the loop
    // bounds here in isolation (that's what caused the mismatch bug).
    //
    // ADDED: a second SetEntityData right after the FIRST chunk only --
    // the real capture shows exactly one extra send at that specific
    // point (immediately after the first level_chunk, not after every
    // chunk), so this fires once, guarded by a local flag, rather than
    // once per chunk in the loop below.
    bool firstChunkSent = false;
    for (int32_t cx = -kSentChunkRadius; cx <= kSentChunkRadius; cx++) {
        for (int32_t cz = -kSentChunkRadius; cz <= kSentChunkRadius; cz++) {
            sendLevelChunk(sock, clientAddr, state, cx, cz);
            if (!firstChunkSent) {
                firstChunkSent = true;
                sendSetEntityData(sock, clientAddr, state);
            }
        }
    }

    // Deliberately NOT sent — each was present in the real capture but
    // skipped here, because its wire format carries enough risk/
    // complexity that sending it wrong seemed worse than not sending it
    // at all (neither looks load-bearing for spawning/rendering; the
    // rest of the once-skipped duplicates -- the second ItemRegistry,
    // second CreativeContent, the 3 extra UpdateAttributes, and the extra
    // SetEntityData after the first chunk -- are NO LONGER skipped, see
    // above):
    //   - PlayerList (#17) — carries full skin/geometry data per entry in
    //     the real capture (5MB+ for one player); tab-list UI only, not
    //     needed to render the world.
    //   - A duplicate NetworkChunkPublisherUpdate (#38) — an
    //     inconsequential repeat in the capture (InventoryContent already
    //     sends all 4 windows above — inventory, ui, offhand, armor — so
    //     there's nothing missing there).

    announcePlayerJoined(state); // multiplayer: introduce this player to others (and vice versa)

    flushGamePackets(sock, clientAddr, state);
}
