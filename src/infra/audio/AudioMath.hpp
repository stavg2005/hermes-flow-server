#pragma once
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <span>
#include <vector>

#include "PcmCast.hpp"

namespace hermes::audio {

class AudioMath {
public:
    static constexpr float MAX_INT16 = 32767.0f;
    static constexpr int32_t CLIP_LIMIT = 30000;

    /**
     * @brief Adds a source buffer into an accumulator buffer.
     * Performs: acc[i] += input[i]
     */
    static void sum_buffers(std::span<int32_t> accumulator, std::span<const int16_t> input) {

        for (size_t i = 0; i < accumulator.size() && i < input.size(); ++i) {
            accumulator[i] += input[i];
        }
    }

    /**
     * @brief Compresses a 32-bit accumulated sample into a 16-bit PCM sample
     * using hyperbolic tangent (tanh) for soft clipping.
     */
    static int16_t soft_clip(int32_t sample) {
        if (sample > CLIP_LIMIT || sample < -CLIP_LIMIT) {
            float compressed = std::tanh(static_cast<float>(sample) / MAX_INT16);
            return static_cast<int16_t>(compressed * MAX_INT16);
        }
        return static_cast<int16_t>(sample);
    }

    /**
     * @brief Batch processes an accumulator into an output buffer.
     */
    static void compress_and_export(std::span<const int32_t> accumulator, std::span<uint8_t> output_bytes) {
        size_t count = output_bytes.size() / sizeof(int16_t);

        for (size_t i = 0; i < count; ++i) {
            pcm::write_sample(output_bytes.data(), i, soft_clip(accumulator[i]));
        }
    }
};
}
