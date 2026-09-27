#include "ResourcePackStackPacket.h"
#include "DataTypeHelper.h"
#include "GamePacketSender.h"
#include <iostream>

using namespace std;

void sendResourcePacksStack(int sock, sockaddr_in clientAddr, ClientState& state) {  
    vector<uint8_t> packet;  
    writeVarInt(packet, 0x07);  
  
    writeBool(packet, false); // Texture Pack Required  
  
    // TexturePacks — a single array, confirmed against gophertunnel's actual
    // ResourcePackStack.Marshal() (v1.59.0): Bool, Slice(TexturePacks),
    // String(BaseGameVersion), SliceUint32Length(Experiments), Bool, Bool.
    // There is only one pack list on this packet, not separate
    // Behaviour/Texture lists — my earlier "fix" that added a second
    // varint(0) here was wrong and needs reverting; it shifted every field
    // after it by one byte.
    writeVarInt(packet, 0); // TexturePacks count — none served
  
    // Base Game Version — must match the version the connecting client is
    // actually running (protocol 2193 == 1.26.51.x here), and must agree
    // with the same field in StartGamePacket.cpp. A stale/mismatched value
    // is a known cause of the client silently rejecting the session.
    writeString(packet, "1.26.51"); // Base Game Version  
  
    // Experiments — per gophertunnel's ResourcePackStack.Marshal(), this
    // uses protocol.SliceUint32Length: a fixed 4-byte little-endian uint32
    // count, NOT a varint.
    writeUInt(packet, 0); // Experiments count (fixed LE uint32, not varint)
    // No experiment entries since count is 0

    // ExperimentsPreviouslyToggled — a required bool field that sits
    // between Experiments and IncludeEditorPacks. This was previously
    // missing entirely, which shifted every byte after it by one and left
    // the packet one byte short of what the client expects — a very likely
    // cause of the client silently bailing right after this packet with no
    // ResourcePackClientResponse at all.
    writeBool(packet, false); // Experiments Previously Toggled

    writeBool(packet, false); // Include Editor Packs  
  
    sendGamePacket(sock, clientAddr, state, packet);  
    cout << "[Bedrock] Sent ResourcePacksStack\n";  

    // Do NOT trigger StartGame here. This used to call
    // completeResourcePackStageAndStartGame() immediately on the theory
    // (per one reading of the wiki) that a client offered zero packs would
    // never bother sending a ResourcePackClientResponse at all, so the
    // server should just skip ahead. Real retail Bedrock clients don't
    // take that shortcut, though -- confirmed against an actual client's
    // log: it always finishes its own resource-pack-stage bookkeeping and
    // sends ClientCacheStatus + ResourcePackClientResponse(Completed) on
    // its own timeline, regardless of whether any packs were offered. When
    // the server jumped ahead and sent StartGame/chunks/inventory before
    // that response arrived, the client received world/spawn data while
    // it still considered itself mid-resource-pack-stage, and reacted by
    // cleanly disconnecting (RakNet ID_DISCONNECTION_NOTIFICATION) instead
    // of continuing to spawn -- it never even got to RequestChunkRadius.
    // The correct, protocol-accurate trigger for StartGame is solely
    // handleResourcePackClientResponse()'s Completed case in
    // ResourcePackClientResponsePacket.cpp; wait for that instead of
    // racing ahead of the client here.
}