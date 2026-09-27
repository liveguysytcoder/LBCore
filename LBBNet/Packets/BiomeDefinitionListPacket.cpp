#include "BiomeDefinitionListPacket.h"
#include "BiomeDefinitionData.h"
#include "DataTypeHelper.h"
#include "GamePacketSender.h"
#include <iostream>

using namespace std;

void sendBiomeDefinitionList(int sock, sockaddr_in clientAddr, ClientState& state) {
    // Gamepacket header (varint) = 122 (0x7A). Field layout below is taken
    // directly from minecraft-data's protocol.json for bedrock 1.26.40
    // (node_modules/minecraft-data/minecraft-data/data/bedrock/1.26.40/protocol.json,
    // types.BiomeDefinition / types.BiomeChunkGeneration), NOT reconstructed
    // from a capture. Confirmed real layout per entry:
    //   name_index        li16 (SIGNED 16-bit) -- NOT varint
    //   biome_id          lu16
    //   temperature       lf32
    //   downfall          lf32
    //   snow_foliage      lf32
    //   depth             lf32
    //   scale             lf32
    //   map_water_colour  li32
    //   rain              bool
    //   tags              option< varint-count array of lu16 > -- option means a
    //                     presence bool comes first; array elements are lu16,
    //                     NOT varint
    //   chunk_generation  option<BiomeChunkGeneration> -- a field this codebase
    //                     was missing entirely. We don't generate real
    //                     chunk-gen data server-side, so we send "absent"
    //                     (a single false byte), but the field must still be
    //                     present on the wire or the client misreads the next
    //                     entry's bytes as this one's contents and desyncs
    //                     for the rest of the packet.
    // Top level (packet_biome_definition_list): VarInt count + biome_definitions[],
    // then VarInt count + string_list[] -- this part was already correct.
    vector<uint8_t> packet;
    writeVarInt(packet, 122);

    writeVarInt(packet, (uint32_t)kBiomeDefinitions.size());
    for (const BiomeDefEntry& def : kBiomeDefinitions) {
        writeShort(packet, (int16_t)def.nameIndex);
        writeUShort(packet, def.biomeId);
        writeFloat(packet, def.temperature);
        writeFloat(packet, def.downfall);
        writeFloat(packet, def.snowFoliage);
        writeFloat(packet, def.depth);
        writeFloat(packet, def.scale);
        writeInt(packet, def.mapWaterColour);
        writeBool(packet, def.rain);

        // tags: option<array<varint count, lu16>>
        bool hasTags = !def.tags.empty();
        writeBool(packet, hasTags);
        if (hasTags) {
            writeVarInt(packet, (uint32_t)def.tags.size());
            for (uint32_t tag : def.tags) writeUShort(packet, (uint16_t)tag);
        }

        // chunk_generation: option<BiomeChunkGeneration> -- always absent for now
        writeBool(packet, false);
    }

    writeVarInt(packet, (uint32_t)kBiomeStringList.size());
    for (const string& s : kBiomeStringList) writeString(packet, s);

    queueGamePacket(state, packet);
    cout << "[Bedrock] Sent BiomeDefinitionList (" << kBiomeDefinitions.size()
         << " biomes, " << kBiomeStringList.size() << " strings)\n";
}

void sendEmptyBiomeDefinitionList(int sock, sockaddr_in clientAddr, ClientState& state) {
    // Same gamepacket ID (122 / 0x7A), payload is just the two top-level
    // array counts, both zero — exactly what marshalling a zero-value
    // packet.BiomeDefinitionList{} produces in the new (non-NBT) format.
    vector<uint8_t> packet;
    writeVarInt(packet, 122);
    writeVarInt(packet, 0); // biome_definitions count
    writeVarInt(packet, 0); // string_list count

    queueGamePacket(state, packet);
    cout << "[Bedrock] Sent empty BiomeDefinitionList (pre-1.21.80 compat)\n";
}
