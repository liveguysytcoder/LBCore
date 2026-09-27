#include "PlayerAuthInputPacket.h"
#include "DataTypeHelper.h"
#include "Multiplayer.h"
#include "ResourcePackClientResponsePacket.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace std;

void handlePlayerAuthInput(int sock, sockaddr_in clientAddr, ClientState& state,
                           const vector<uint8_t>& data, size_t& offset) {
    // ---- front block (unchanged from the earlier, real-client-verified parse) ----
    float pitch = readFloat(data, offset);
    float yaw   = readFloat(data, offset);
    float posX  = readFloat(data, offset);
    float posY  = readFloat(data, offset);
    float posZ  = readFloat(data, offset);
    (void)readFloat(data, offset);         // move_vector.x
    (void)readFloat(data, offset);         // move_vector.z
    float headYaw = readFloat(data, offset);

    // Fallback release for the second spawn phase: a client that is already
    // sending input but never sent SetLocalPlayerAsInitialised must not be left
    // waiting forever. The log line says which trigger fired, which shows whether
    // the client survived phase 1 on its own.
    if (state.spawnSequenceSent && !state.spawnPhase2Sent)
        sendSpawnSequencePhase2(sock, clientAddr, state,
            "first PlayerAuthInput (client did NOT send SetLocalPlayerAsInitialised)");

    bool moved = !state.receivedAuthInput
              || fabsf(posX - state.posX) > 0.001f || fabsf(posY - state.posY) > 0.001f
              || fabsf(posZ - state.posZ) > 0.001f || fabsf(yaw - state.yaw) > 0.01f
              || fabsf(pitch - state.pitch) > 0.01f || fabsf(headYaw - state.headYaw) > 0.01f;

    state.pitch = pitch;
    state.yaw = yaw;
    state.posX = posX;
    state.posY = posY;
    state.posZ = posZ;
    state.headYaw = headYaw;
    state.receivedAuthInput = true;

    // ---- extended fields (best effort; a misparse here must never lose the
    // position update above, so failures are swallowed) ----
    try {
        uint32_t flagCount = readVarInt(data, offset);
        if (flagCount <= 64) {                        // sanity: real clients send far fewer
            uint64_t mask = 0;
            for (uint32_t i = 0; i < flagCount; i++) {
                int32_t id = readZigZag32(data, offset);
                if (id >= 0 && id < 64) mask |= (1ULL << id);
            }
            state.inputFlagMask = mask;
            state.inputMode = readVarInt(data, offset);
            state.playMode = readVarInt(data, offset);
            (void)readZigZag32(data, offset);         // interaction model
            (void)readFloat(data, offset);            // interact pitch
            (void)readFloat(data, offset);            // interact yaw
            state.lastAuthInputTick = readVarInt64(data, offset);
            state.deltaX = readFloat(data, offset);
            state.deltaY = readFloat(data, offset);
            state.deltaZ = readFloat(data, offset);
        }
    } catch (const out_of_range&) {
        // shorter than expected: keep whatever was parsed
    }

    // Log the first input and then only every 100th, instead of ~20 lines/sec.
    if (state.authInputCount++ % 100 == 0) {
        cout << "[Bedrock] PlayerAuthInput: pos=(" << posX << ", " << posY << ", " << posZ
             << ") yaw=" << yaw << " pitch=" << pitch << " tick=" << state.lastAuthInputTick
             << " flags=" << state.inputFlagMask << "\n";
    }

    streamChunksAround(sock, clientAddr, state);
    if (moved || state.authInputCount % 20 == 0) broadcastMovement(state);
}
