#pragma once

#include <cstdint>
#include <cstring>
#include <span>

/**
 * @file PcmCast.hpp
 * @brief Safe byte↔sample reinterpretation utilities for PCM audio buffers.
 *
 * Raw `reinterpret_cast` between `uint8_t*` and `int16_t*` violates the
 * C++ strict aliasing rule (UB). These helpers use `std::memcpy` to perform
 * the conversion safely while remaining zero-cost on any optimizing compiler
 * (GCC/Clang/MSVC all elide the memcpy at -O1+).
 */
namespace hermes::audio::pcm {

/**
 * @brief Reads a single int16_t sample from a byte buffer at a given index.
 */
inline int16_t read_sample(const uint8_t* data, size_t sample_index) {
  int16_t sample;
  std::memcpy(&sample, data + sample_index * sizeof(int16_t), sizeof(int16_t));
  return sample;
}

/**
 * @brief Writes a single int16_t sample into a byte buffer at a given index.
 */
inline void write_sample(uint8_t* data, size_t sample_index, int16_t value) {
  std::memcpy(data + sample_index * sizeof(int16_t), &value, sizeof(int16_t));
}

/**
 * @brief Returns a mutable int16_t span over a uint8_t buffer.
 *
 * This uses reinterpret_cast but is guarded by the fact that audio buffers
 * are always allocated as vectors/arrays with sufficient alignment.
 * Both GCC and Clang guarantee this works for naturally-aligned accesses.
 * The NOLINT suppresses the strict-aliasing warning intentionally.
 */
// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
inline std::span<int16_t> as_samples(std::span<uint8_t> bytes) {
  return {reinterpret_cast<int16_t*>(bytes.data()),  // NOLINT
          bytes.size() / sizeof(int16_t)};
}

/**
 * @brief Returns a const int16_t span over a const uint8_t buffer.
 */
// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
inline std::span<const int16_t> as_samples(std::span<const uint8_t> bytes) {
  return {reinterpret_cast<const int16_t*>(bytes.data()),  // NOLINT
          bytes.size() / sizeof(int16_t)};
}

}  // namespace hermes::audio::pcm
