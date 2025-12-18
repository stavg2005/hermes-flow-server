#pragma once
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "spdlog/spdlog.h"

namespace WavUtils {

// Returns the offset to the start of audio data (after "data" chunk size)
// Returns 44 (default) if not found or invalid.
inline size_t GetAudioDataOffset(std::span<const uint8_t> buffer) {
    if (buffer.size() < 44) return 44;  // Too small, assume standard

    // 1. Check for "RIFF"
    if (std::memcmp(buffer.data(), "RIFF", 4) != 0) return 0;  // Raw PCM?

    size_t pos = 12;  // Skip RIFF + Size + WAVE

    // Loop through chunks
    while (pos + 8 < buffer.size()) {
        // Read Chunk ID
        const uint8_t* chunk_id = buffer.data() + pos;

        // Read Chunk Size (Little Endian)
        uint32_t chunk_size = 0;
        std::memcpy(&chunk_size, buffer.data() + pos + 4, 4);

        // Found "data" chunk?
        if (std::memcmp(chunk_id, "data", 4) == 0) {
            return pos + 8;  // Audio starts immediately after size
        }

        // Skip this chunk
        pos += 8 + chunk_size;
    }

    return 44;  // Fallback
}
}  // namespace WavUtils
