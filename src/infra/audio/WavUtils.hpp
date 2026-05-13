#pragma once
#include <cstdint>
#include <cstring>
#include <span>

#include "spdlog/spdlog.h"

namespace hermes::audio::wav {

// Returns the offset to the start of audio data (after "data" chunk size)
// Returns 44 (default) if not found or invalid.
inline size_t get_audio_data_offset(std::span<const uint8_t> buffer) {
    constexpr size_t DEFAULT_WAV_HEADER_SIZE = 44;
    constexpr size_t WAV_CHUNK_OFFSET = 12;
    constexpr size_t WAV_CHUNK_HEADER_SIZE = 8;

    if (buffer.size() < DEFAULT_WAV_HEADER_SIZE) {
        return DEFAULT_WAV_HEADER_SIZE;  // Too small, assume standard
    }

    if (std::memcmp(buffer.data(), "RIFF", 4) != 0) {
        return 0;
    }

    size_t pos = WAV_CHUNK_OFFSET;  // Skip RIFF + Size + WAVE

    while (pos + WAV_CHUNK_HEADER_SIZE < buffer.size()) {
        const uint8_t* chunk_id = buffer.data() + pos;

        uint32_t chunk_size = 0;
        std::memcpy(&chunk_size, buffer.data() + pos + 4, 4);

        if (std::memcmp(chunk_id, "data", 4) == 0) {
            return pos + WAV_CHUNK_HEADER_SIZE;  // Audio starts immediately after size
        }

        size_t next_pos = pos + WAV_CHUNK_HEADER_SIZE + chunk_size;
        if (next_pos > buffer.size() || next_pos < pos) {
            spdlog::warn("WavUtils: Malformed chunk size at offset {}", pos);
            return DEFAULT_WAV_HEADER_SIZE;  // Fallback
        }
        pos = next_pos;
    }

    return DEFAULT_WAV_HEADER_SIZE;  // Fallback
}
}
