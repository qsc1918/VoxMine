#pragma once
#include <cstdint>

namespace gen {
// Generates a full chunk column (16 x WORLD_HEIGHT x 16) of block ids.
// Deterministic given (seed, cx, cz). Out points to CHUNK_VOL bytes.
void generateColumn(uint32_t seed, int cx, int cz, uint8_t* out);
}
