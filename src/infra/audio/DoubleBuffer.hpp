#pragma once

#include <array>
#include <atomic>
#include <filesystem>
#include <span>
#include <vector>

namespace hermes::infra {

/**
 * @brief Async double-buffer for audio streaming.
 *
 * =========================================================================
 * THREAD SAFETY CONTRACT (Single-Producer, Single-Consumer)
 * =========================================================================
 * - Producer Thread (e.g., S3 Loader Coroutine):
 * 1. May ONLY write to the buffer returned by `get_write_span()`.
 * 2. May ONLY access `get_write_span()` when `back_buffer_ready_` is FALSE.
 * 3. Sets `back_buffer_ready_` to TRUE (with release semantics) when finished.
 *
 * - Consumer Thread (e.g., Audio Executor):
 * 1. May ONLY read from the buffer returned by `get_read_span()`.
 * 2. Checks `back_buffer_ready_` (with acquire semantics).
 * 3. `swap()` and `reset()` MUST ONLY be called by the Consumer Thread.
 * 4. After calling `swap()`, the Consumer sets `back_buffer_ready_` to FALSE.
 * =========================================================================
 */
struct DoubleBuffer {
  std::filesystem::path path_;

  /**
   * @brief Atomic flag indicating the back buffer is full.
   */
  std::atomic<bool> back_buffer_ready_{false};

  DoubleBuffer();

  DoubleBuffer(const DoubleBuffer&) = delete;
  DoubleBuffer& operator=(const DoubleBuffer&) = delete;

  std::span<uint8_t> get_read_span();
  std::span<uint8_t> get_write_span();

  void set_read_index(int value);
  void swap();
  void reset();

 private:
  std::array<std::vector<uint8_t>, 2> blocks_;

  
  std::atomic<int> read_index_{0};
};

}  // namespace hermes::infra
