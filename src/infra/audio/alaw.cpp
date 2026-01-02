#include <Alaw.hpp>
#include <array>
#include <cstdint>
#include <limits>
#include <boost/core/span.hpp>
#include <cstring>

static constexpr int16_t MIN_16BIT_VALUE = std::numeric_limits<int16_t>::min();
static constexpr int16_t MAX_16BIT_VALUE = std::numeric_limits<int16_t>::max();
static constexpr uint32_t VALUE_COUNT_16BIT =
    MAX_16BIT_VALUE - MIN_16BIT_VALUE + 1;

static constexpr uint8_t MIN_U8BIT_VALUE = std::numeric_limits<uint8_t>::min();
static constexpr uint8_t MAX_U8BIT_VALUE = std::numeric_limits<uint8_t>::max();
static constexpr uint16_t VALUE_COUNT_U8BIT =
    MAX_U8BIT_VALUE - MIN_U8BIT_VALUE + 1;

static constexpr uint8_t BASE_ALAW_MASK = 0b0101'0101;
static constexpr uint8_t QUANT_MASK = 0b0000'1111;
static constexpr uint8_t SIGN_BIT_MASK = 0b1000'0000;
static constexpr uint8_t SEGMENT_SHIFT = 4;

static uint16_t get_encode_table_idx(const int16_t pcm_sample) {
  static constexpr uint16_t PCM_IDX_DIFF = -std::numeric_limits<int16_t>::min();
  const uint16_t idx = pcm_sample + PCM_IDX_DIFF;

  return idx;
}

// The alaw algorithm requires signed bitwise operations.
// NOLINTBEGIN(hicpp-signed-bitwise)

static std::array<uint8_t, VALUE_COUNT_16BIT> make_encode_table() {
  // We're initializing the array with garbage values and populating it later
  // on. NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init,
  // hicpp-member-init)
  std::array<uint8_t, VALUE_COUNT_16BIT> encode_table;

  // Using wide_pcm_sample because we need it to reach MAX_16BIT_VALUE + 1 for
  // the loop's exit condition.
  for (int32_t wide_pcm_sample = MIN_16BIT_VALUE;
       wide_pcm_sample <= MAX_16BIT_VALUE; wide_pcm_sample++) {
    const auto pcm_sample = static_cast<int16_t>(wide_pcm_sample);

    uint8_t alaw_mask;
    int16_t normalized_pcm_sample;
    if (pcm_sample >= 0) {
      static constexpr uint8_t SIGNED_ALAW_MASK =
          BASE_ALAW_MASK | SIGN_BIT_MASK;
      alaw_mask = SIGNED_ALAW_MASK;
      normalized_pcm_sample = pcm_sample;
    } else {
      alaw_mask = BASE_ALAW_MASK;
      normalized_pcm_sample = -pcm_sample - 8;
    }

    // Convert the scaled magnitude to segment number.
    static constexpr uint8_t SEGMENT_COUNT = 8;
    static const std::array<int16_t, SEGMENT_COUNT> SEGMENT_EDGES = {
        0xFF, 0x1FF, 0x3FF, 0x7FF, 0xFFF, 0x1FFF, 0x3FFF, 0x7FFF};
    uint8_t segment_idx;
    for (segment_idx = 0; segment_idx < SEGMENT_COUNT; segment_idx++) {
      if (normalized_pcm_sample <= SEGMENT_EDGES.at(segment_idx)) {
        break;
      }
    }

    uint8_t unmasked_alaw_sample;
    if (segment_idx == SEGMENT_COUNT) {
      // Out of range, assign maximum value:
      static constexpr uint8_t MAX_VALUE = 0b0111'1111;
      unmasked_alaw_sample = MAX_VALUE;
    } else {
      // Combine the sign, segment and quantization bits:
      uint8_t pcm_shift;
      if (segment_idx == 0) {
        static constexpr uint8_t FIRST_SEGMENT_PCM_SHIFT = 4;
        pcm_shift = FIRST_SEGMENT_PCM_SHIFT;
      } else {
        static constexpr uint8_t BASE_PCM_SHIFT = 3;
        pcm_shift = BASE_PCM_SHIFT + segment_idx;
      }

      const uint8_t shifted_pcm =
          (normalized_pcm_sample >> pcm_shift) & QUANT_MASK;
      const uint8_t shifted_segment = segment_idx << SEGMENT_SHIFT;

      unmasked_alaw_sample = shifted_pcm | shifted_segment;
    }

    const uint8_t alaw_sample = unmasked_alaw_sample ^ alaw_mask;

    const auto encode_table_idx = get_encode_table_idx(pcm_sample);
    encode_table.at(encode_table_idx) = alaw_sample;
  }

  return encode_table;
}

static std::array<uint16_t, VALUE_COUNT_U8BIT> make_decode_table() {
  // We're initializing the array with garbage values and populating it later
  // on.
  // hicpp-member-init)
  std::array<uint16_t, VALUE_COUNT_U8BIT> decode_table{};

  // Using wide_alaw_sample because we need it to reach VALUE_COUNT_U8BIT + 1
  // for the loop's exit condition.
  for (uint16_t wide_alaw_sample = MIN_U8BIT_VALUE;
       wide_alaw_sample <= MAX_U8BIT_VALUE; wide_alaw_sample++) {
    const auto alaw_sample = static_cast<uint8_t>(wide_alaw_sample);

    const auto masked_alaw_sample = alaw_sample ^ BASE_ALAW_MASK;

    static constexpr uint8_t QUANT_SHIFT = 4;
    int16_t pcm_sample = (masked_alaw_sample & QUANT_MASK) << QUANT_SHIFT;

    static constexpr uint8_t SEGMENT_MASK = 0b0111'0000;
    const int16_t segment_idx =
        (masked_alaw_sample & SEGMENT_MASK) >> SEGMENT_SHIFT;

    if (segment_idx == 0) {
      pcm_sample += 0x008;
    } else {
      pcm_sample += 0x108;
    }

    if (segment_idx > 1) {
      pcm_sample <<= segment_idx - 1;
    }

    const bool alaw_sign_bit = masked_alaw_sample & SIGN_BIT_MASK;
    if (!alaw_sign_bit) {
      pcm_sample = -pcm_sample;
    }

    decode_table.at(alaw_sample) = pcm_sample;
  }

  return decode_table;
}

// NOLINTEND(hicpp-signed-bitwise)

void encode_alaw(boost::span<const int16_t> pcm,
                 boost::span<uint8_t> alawOut) {
  // Ensure output span is large enough
  if (pcm.size() > alawOut.size()) {
    return; // Or throw an exception based on your error handling policy
  }

  static const auto encode_table = make_encode_table();

  for (std::size_t i = 0; i < pcm.size(); ++i) {
    const auto encode_table_idx = get_encode_table_idx(pcm[i]);
    alawOut[i] = encode_table.at(encode_table_idx);
  }
}


void decode_alaw(boost::span<const uint8_t> alaw,
                 boost::span<int16_t> pcmOut) {
  // Ensure output span is large enough
  if (alaw.size() > pcmOut.size()) {
    return; // Or throw an exception based on your error handling policy
  }

  static const auto decode_table = make_decode_table();

  for (std::size_t i = 0; i < alaw.size(); ++i) {
    pcmOut[i] = decode_table.at(alaw[i]);
  }
}
