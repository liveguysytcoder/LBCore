#include "InventoryContentPacket.h"
#include "DataTypeHelper.h"
#include "GamePacketSender.h"
#include <iostream>

using namespace std;

// The canonical encoding of "no item", exactly as the reference writes it:
// int16 id 0, uint16 count 0, varuint metadata 0, hasStackNetID false,
// varuint block runtime id 0, and an EMPTY extra-data byte slice (one 0 byte).
static void appendEmptyItem(vector<uint8_t>& p) {
    writeShort(p, 0);
    writeUShort(p, 0);
    writeVarInt(p, 0);
    writeBool(p, false);
    writeVarInt(p, 0);
    writeVarInt(p, 0);
}

// Every real server sends a window at its full size (empty slots included);
// a 0-slot window leaves the client's inventory with nothing to index.
static uint32_t slotCountFor(int32_t windowId) {
    switch (windowId) {
        case InventoryWindowId::Inventory: return 36;
        case InventoryWindowId::UI:        return 54;
        case InventoryWindowId::OffHand:   return 1;
        case InventoryWindowId::Armor:     return 4;
        default:                           return 36;
    }
}

void sendInventoryContent(int sock, sockaddr_in clientAddr, ClientState& state, int32_t windowId) {
    (void)sock; (void)clientAddr;
    // Gamepacket header (varint) for InventoryContent = 49 (0x31).
    vector<uint8_t> packet;
    writeVarInt(packet, 49);

    writeVarInt(packet, (uint32_t)windowId); // window_id (WindowIDVarint)
    const uint32_t slots = slotCountFor(windowId);
    writeVarInt(packet, slots);              // content: one entry per slot (all empty)
    for (uint32_t i = 0; i < slots; i++) appendEmptyItem(packet);

    // FIXED (confirmed against real protocol.json for 1.26.40): container_id
    // "Container" (FullContainerName: a container_id byte + an optional
    // dynamic id) was previously sent here as a set of made-up non-zero
    // values (29, 0, 34, 6), with a comment claiming they were "confirmed
    // via real Dragonfly capture." That claim doesn't hold up: reading
    // Dragonfly's actual source (session/player.go, sendInv/sendItem/
    // broadcastOffHandFunc/broadcastArmourFunc -- every call site that
    // builds a packet.InventoryContent{} for these exact 4 windows) shows
    // it never sets this field at all, for any of them, including the
    // player's own main inventory. The Go zero value for a byte is 0, so
    // the real wire behaviour is container_id = 0 (NONE) with no dynamic
    // id present, for every window -- not a per-window value. Likely origin
    // of the mistake: 29/34/6 don't correspond to anything meaningful in
    // Mojang's own ContainerType enum either (29 = BLAST_FURNACE, 34 =
    // JIGSAW_EDITOR, 6 = BREWING_STAND -- none of which are the inventory,
    // offhand or armor window they were attached to).
    writeUByte(packet, 0);    // FullContainerName.container_id = NONE, for every window
    writeBool(packet, false); // FullContainerName.dynamic_container_id -- absent

    // FIXED: storage_item is a full ItemV4 struct (network_id as li16,
    // count as lu16, metadata as varint, has_stack_id as bool,
    // block_runtime_id as varint, then an always-present length-prefixed
    // "extra" struct) -- it is never abbreviated to a single field, even
    // for an empty/air item. Previously this was collapsed to one
    // writeVarInt(0), which is exactly why the client's parser choked
    // trying to read network_id as a 2-byte li16 and found unrelated
    // varint bytes instead.
    appendEmptyItem(packet);  // storage_item: no item
    queueGamePacket(state, packet);
    cout << "[Bedrock] Sent InventoryContent(window=" << windowId << ")\n";
}
