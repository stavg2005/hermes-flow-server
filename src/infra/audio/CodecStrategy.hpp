#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

#include "alaw.hpp"

/**
 * @brief Interface for Audio Encoding Algorithms.
 *  @details
 * To add a new Codec (e.g., Opus):
 * 1. Implement this interface.
 * 2. Return the correct Payload Type (e.g., 111 for Opus dynamic).
 * 3. Update 'GetTimestampIncrement' (Opus 20ms @ 48kHz = 960 ticks).
 * 4. Inject the new strategy into 'RTPStreamer'.
 */
struct ICodecStrategy {
    virtual ~ICodecStrategy() = default;

    /**
     * @brief Encodes raw PCM data into the output buffer. pcm is expected to contain native-endian,
     * 16-bit aligned PCM samples.
     * @param pcm Input PCM data (16-bit).
     * @param out_buffer Destination buffer for encoded bytes.
     * @return Number of bytes written to out_buffer.
     */
    virtual size_t Encode(std::span<const uint8_t> pcm, std::span<uint8_t> out_buffer) = 0;

    // Metadata required by RTP Packetizer
    virtual uint8_t GetPayloadType() const = 0;
    virtual uint32_t GetTimestampIncrement(size_t pcm_byte_size) const = 0;
};

// 2. Concrete A-Law Strategy
struct ALawCodecStrategy : ICodecStrategy {
    size_t Encode(std::span<const uint8_t> pcm, std::span<uint8_t> out_buffer) override {
        const size_t sample_count = pcm.size() / sizeof(int16_t);

        if (out_buffer.size() < sample_count) {
            return 0;
        }

        auto samples =
            std::span<const int16_t>(reinterpret_cast<const int16_t*>(pcm.data()), sample_count);

        encode_alaw(samples, out_buffer);

        return sample_count;  // A-Law is 1 byte per sample
    }

    uint8_t GetPayloadType() const override {
        return 8;  // PCMA
    }

    uint32_t GetTimestampIncrement(size_t pcm_byte_size) const override {
        // For A-Law/PCM, 1 sample = 1 timestamp tick
        return static_cast<uint32_t>(pcm_byte_size / sizeof(int16_t));
    }
};
