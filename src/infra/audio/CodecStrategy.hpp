#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

#include "Alaw.hpp"
#include "PcmCast.hpp"
namespace hermes::audio {
/**
 * @brief Interface for Audio Encoding Algorithms.
 */
struct ICodecStrategy {
  virtual ~ICodecStrategy() = default;

  /**
   * @brief Encodes raw PCM data into the output buffer. pcm is expected to
   * contain native-endian, 16-bit aligned PCM samples.
   * @param pcm Input PCM data (16-bit).
   * @param out_buffer Destination buffer for encoded bytes.
   * @return Number of bytes written to out_buffer.
   */
  virtual size_t encode(std::span<const uint8_t> pcm,
                        std::span<uint8_t> out_buffer) = 0;

  // Metadata required by RTP Packetizer
  virtual uint8_t get_payload_type() const = 0;

  /**
   * @brief Calculates the RTP timestamp increment for a given PCM chunk.
   *
   * For most codecs (like G.711 or PCM), the timestamp increments by the number
   * of samples. For example, 20ms of 8kHz audio = 160 samples = 160 ticks.
   *
   * @param pcm_byte_size Size of the raw PCM input in bytes.
   */
  virtual uint32_t get_timestamp_increment(size_t pcm_byte_size) const = 0;
};

struct ALawCodecStrategy : ICodecStrategy {
  size_t encode(std::span<const uint8_t> pcm,
                std::span<uint8_t> out_buffer) override {
    const size_t sample_count = pcm.size() / sizeof(int16_t);

    if (out_buffer.size() < sample_count) {
      return 0;
    }

    auto samples = pcm::as_samples(pcm);

    encode_alaw(samples, out_buffer);

    return sample_count;  // A-Law is 1 byte per sample
  }

  uint8_t get_payload_type() const override {
    return 8;  // PCMA
  }

  uint32_t get_timestamp_increment(size_t pcm_byte_size) const override {
    // For A-Law/PCM, 1 sample = 1 timestamp tick
    return static_cast<uint32_t>(pcm_byte_size / sizeof(int16_t));
  }
};
}  // namespace hermes::audio
