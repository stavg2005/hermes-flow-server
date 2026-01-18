#pragma once
#include <boost/core/span.hpp>
#include <cstddef>
#include <cstdint>
namespace hermes::audio {
/**
 * @brief  G.711a A-Law encoder (LUT-based).
 * @param pcm Input buffer of 16-bit signed integers.
 * @param alawOut Output buffer. Must be at least `pcm.size()` bytes.
 */
void EncodeAlaw(boost::span<const int16_t> pcm, boost::span<uint8_t> alawOut);

/* Fast in-place encoder.
 *buf      : pointer to a buffer that *presently* holds PCM-16 samples
 *   pcmBytes : how many PCM bytes are valid in the buffer (must be even)
 * Returns     number of encoded bytes now valid at buf[0..N-1].              */
std::size_t EncodeAlawInplace(uint8_t* buf, std::size_t pcmBytes);

/* (Optional) Decode A-law back to PCM-16 */
void DecodeAlaw(boost::span<const uint8_t> alaw, boost::span<int16_t> pcmOut);
}  // namespace hermes::audio
