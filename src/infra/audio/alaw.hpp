#pragma once
#include <boost/core/span.hpp>
#include <cstddef>
#include <cstdint>

/**
 * @brief Encodes 16-bit PCM samples into 8-bit A-Law (G.711a).
 * * Uses a pre-computed lookup table for O(1) encoding performance.
 * This effectively compresses audio by 50% with minimal quality loss for speech.
 * * @param pcm Input buffer of 16-bit signed integers.
 * @param alawOut Output buffer. Must be at least `pcm.size()` bytes.
 */
void encode_alaw(boost::span<const int16_t> pcm, boost::span<uint8_t> alawOut);

/* Fast in-place encoder.
 *   buf      : pointer to a buffer that *presently* holds PCM-16 samples
 *   pcmBytes : how many PCM bytes are valid in the buffer (must be even)
 * Returns     number of encoded bytes now valid at buf[0..N-1].              */
std::size_t encode_alaw_inplace(uint8_t* buf, std::size_t pcmBytes);

/* (Optional) Decode A-law back to PCM-16 */
void decode_alaw(boost::span<const uint8_t> alaw, boost::span<int16_t> pcmOut);
