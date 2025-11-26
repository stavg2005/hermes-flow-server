#pragma once
#include <boost/core/span.hpp>
#include <cstdint>
#include <cstddef>

/* Encode PCM-16 (little-endian) to A-law.
 *   pcm      : span of 16-bit samples
 *   alawOut  : span of equal length (bytes) that will receive the codes      */
void encode_alaw(boost::span<const int16_t> pcm,
                 boost::span<uint8_t>       alawOut);

/* Fast in-place encoder.
 *   buf      : pointer to a buffer that *presently* holds PCM-16 samples
 *   pcmBytes : how many PCM bytes are valid in the buffer (must be even)
 * Returns     number of encoded bytes now valid at buf[0..N-1].              */
std::size_t encode_alaw_inplace(uint8_t* buf,
                                std::size_t pcmBytes);

/* (Optional) Decode A-law back to PCM-16 */
void decode_alaw(boost::span<const uint8_t> alaw,
                 boost::span<int16_t>       pcmOut);
