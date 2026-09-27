#include "CreativeContentPacket.h"
#include "DataTypeHelper.h"
#include "GamePacketSender.h"
#include <iostream>

using namespace std;

namespace {

// Writes one ItemStack exactly as gophertunnel's Writer.Item() does
// (minecraft/protocol/writer.go, confirmed by reading it directly):
//   NetworkID       varint32 (zigzag)
//   Count           uint16 (LE)
//   MetadataValue   varuint32
//   BlockRuntimeID  varint32 (zigzag)
//   itemUserData    -- if NetworkID == 0 (empty stack): a single
//                      varuint32(0) byte and nothing else.
//                   -- otherwise: a varuint32-length-prefixed blob
//                      containing: int16 NBT-length (0 = "no NBT" here,
//                      since none of the items below carry NBT),
//                      then CanBePlacedOn count as a PLAIN uint32 (NOT
//                      varint -- confirmed via FuncSliceUint32Length in
//                      gophertunnel's io.go, same "no size compression"
//                      pattern already established for
//                      NetworkChunkPublisherUpdate's chunk list), then
//                      CanBreak count the same way. BlockingTick (int64)
//                      is only present for the shield item, which none
//                      of these are, so it's omitted entirely.
// itemId is the SAME runtime ID already sent to the client in
// ItemRegistry (see ItemRegistryData.cpp) -- items are referenced by
// that id everywhere else in the protocol, including here.
void writeItemStack(vector<uint8_t>& buf, int32_t itemId, uint16_t count) {
    writeZigZag32(buf, itemId);   // NetworkID
    writeUShort(buf, count);      // Count
    writeVarInt(buf, 0);          // MetadataValue (aux/damage value)
    writeZigZag32(buf, 0);        // BlockRuntimeID (not a block)

    if (itemId == 0) {
        writeVarInt(buf, 0);      // empty stack: itemUserData = varuint32(0)
        return;
    }

    vector<uint8_t> extra;
    writeShort(extra, 0);   // NBT length = 0 (no NBT on any of these)
    writeUInt(extra, 0);    // CanBePlacedOn: count = 0 (plain uint32)
    writeUInt(extra, 0);    // CanBreak: count = 0 (plain uint32)

    writeVarInt(buf, (uint32_t)extra.size());
    buf.insert(buf.end(), extra.begin(), extra.end());
}

// A small, real starter set of the Creative inventory: basic building
// blocks, tools, food, and other commonly-needed items. Item IDs here
// are pulled directly from this project's own ItemRegistryData.cpp
// table (parsed and cross-checked against the real vanilla names), so
// they're guaranteed to be the SAME ids the client was already told
// about in ItemRegistry -- nothing invented or guessed.
struct CreativeEntry { const char* name; int32_t id; };
constexpr CreativeEntry kCreativeItems[] = {
    {"dirt",              3},
    {"stone",              1},
    {"cobblestone",        4},
    {"oak_planks",         5},
    {"oak_log",           17},
    {"glass",             20},
    {"sand",              12},
    {"gravel",            13},
    {"crafting_table",    58},
    {"furnace",           61},
    {"torch",             50},
    {"ladder",             65},
    {"cobblestone_wall",  139},
    {"stone_bricks",       98},
    {"brick_block",        45},
    {"obsidian",           49},
    {"bedrock",             7},
    {"grass_block",         2},
    {"glowstone",          89},
    {"oak_fence",          85},
    {"wool",              796},
    {"leaves",             813},
    {"wooden_axe",        342},
    {"iron_pickaxe",      328},
    {"diamond_sword",     347},
    {"bow",                331},
    {"arrow",              332},
    {"shears",             453},
    {"bucket",             392},
    {"water_bucket",       394},
    {"lava_bucket",        395},
    {"iron_ingot",         336},
    {"gold_ingot",         337},
    {"diamond",            335},
    {"emerald",            552},
    {"redstone",           405},
    {"coal",               333},
    {"bone_meal",          443},
    {"apple",              285},
    {"bread",              290},
    {"cooked_beef",        303},
};
constexpr size_t kCreativeItemCount = sizeof(kCreativeItems) / sizeof(kCreativeItems[0]);

} // namespace

// CONFIRMED against gophertunnel's real source (minecraft/conn.go): the
// server automatically replies to RequestChunkRadius with a genuinely
// EMPTY `&packet.CreativeContent{}` as part of the StartGame handshake,
// before SetLocalPlayerAsInitialised is even received. The real,
// populated item list comes later -- see sendCreativeContentWithItems
// above, called once Phase 2 begins.
void sendCreativeContent(int sock, sockaddr_in clientAddr, ClientState& state) {
    vector<uint8_t> packet;
    writeVarInt(packet, 145);   // CreativeContent
    writeVarInt(packet, 0);     // Groups: array count = 0
    writeVarInt(packet, 0);     // Entries: array count = 0
    queueGamePacket(state, packet);
    cout << "[Bedrock] Sent CreativeContent (empty -- matches gophertunnel's handshake default)\n";
}

// The REAL, populated Creative item list. Confirmed against Dragonfly's
// own session.New() (server/session/session.go): this is sent AFTER
// SetLocalPlayerAsInitialised, during session setup -- NOT during the
// initial StartGame handshake. See sendCreativeContent() below for the
// one that belongs in the handshake itself.
void sendCreativeContentWithItems(int sock, sockaddr_in clientAddr, ClientState& state) {
    // Gamepacket header (varint) for CreativeContent = 145 (0x91).
    vector<uint8_t> packet;
    writeVarInt(packet, 145);

    // Groups (ordinal 0): confirmed against gophertunnel's creative.go --
    // EVERY creative item must belong to a group, even if that means a
    // single "anonymous" one (empty name, empty icon), or the client
    // will refuse to render the tab at all. One anonymous Construction-
    // category group is enough for a flat, ungrouped item list.
    writeVarInt(packet, 1);              // Groups: array count = 1
    writeUByte(packet, 1);               // Category = Construction (1)
    writeString(packet, "");             // Name = "" (anonymous group)
    writeItemStack(packet, 0, 0);        // Icon = empty item stack

    // Entries (ordinal 1): CreativeItemNetId (varuint32, unique per
    // entry), Item Instance (ItemStack), Group Index (varuint32 -- 0,
    // since there's only the one anonymous group above).
    writeVarInt(packet, (uint32_t)kCreativeItemCount); // Entries: array count
    for (uint32_t i = 0; i < kCreativeItemCount; i++) {
        writeVarInt(packet, i);                         // Creative Net Id
        writeItemStack(packet, kCreativeItems[i].id, 1); // Item Instance, count=1
        writeVarInt(packet, 0);                          // Group Index = 0
    }

    queueGamePacket(state, packet);
    cout << "[Bedrock] Sent CreativeContent (" << kCreativeItemCount << " items)\n";
}
