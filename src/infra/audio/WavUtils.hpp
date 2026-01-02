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


    if (std::memcmp(buffer.data(), "RIFF", 4) != 0) return 0;

    size_t pos = 12;  // Skip RIFF + Size + WAVE


    while (pos + 8 < buffer.size()) {

        const uint8_t* chunk_id = buffer.data() + pos;


        uint32_t chunk_size = 0;
        std::memcpy(&chunk_size, buffer.data() + pos + 4, 4);


        if (std::memcmp(chunk_id, "data", 4) == 0) {
            return pos + 8;  // Audio starts immediately after size
        }


        size_t next_pos = pos + 8 + chunk_size;
        if (next_pos > buffer.size() || next_pos < pos) {
            // Check for overflow/malformed size
            spdlog::warn("WavUtils: Malformed chunk size at offset {}", pos);
            return 44;  // Fallback
        }
        pos = next_pos;
    }

    return 44;  // Fallback
}
} 
