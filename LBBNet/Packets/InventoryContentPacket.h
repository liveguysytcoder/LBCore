#pragma once
#include <cstdint>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "../../LBNet/Server/Clients.h"

using namespace std;

// Classic fixed pseudo-window IDs for the player's own containers -- these
// aren't part of the general ContainerID enum used elsewhere (crafting
// grids, furnaces, etc.), they're special-cased IDs the client already
// knows to associate with its own inventory/armor/offhand without the
// server needing to open a container for them first.
// Sends InventoryContent (49 / 0x31) to the client -- tells it what's in
// one of its own containers (main inventory, armor, offhand, ui). This
// project had no packet for this at all; every player container was left
// completely undeclared. That's the leading suspect for the crash that
// happens right as PlayStatus(PlayerSpawn) is processed: that's exactly
// the moment the client needs to build its inventory/hotbar HUD, and with
// nothing ever telling it what those containers hold, it has nothing to
// build them from.
//
// ADDED: a 4th window, "ui" -- confirmed against a real Dragonfly capture
// that a working server sends exactly 4 InventoryContent packets per
// player (inventory, ui, offhand, armor), not 3. Missing this one was a
// real gap, not just an assumption.
namespace InventoryWindowId {
    constexpr int32_t Inventory = 0;
    constexpr int32_t UI        = 124;
    constexpr int32_t OffHand   = 119;
    constexpr int32_t Armor     = 120;
}

// Sends InventoryContent (49 / 0x31) to the client -- tells it what's in
// one of its own containers (main inventory, armor, offhand). This project
// had no packet for this at all; every player container was left
// completely undeclared. That's the leading suspect for the crash that
// happens right as PlayStatus(PlayerSpawn) is processed: that's exactly
// the moment the client needs to build its inventory/hotbar HUD, and with
// nothing ever telling it what those containers hold, it has nothing to
// build them from.
//
// This sends empty containers (itemCount = 0), which is correct for a
// fresh player with nothing in their inventory yet.
void sendInventoryContent(int sock, sockaddr_in clientAddr, ClientState& state, int32_t windowId);
