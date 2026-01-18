#include <stdlib.h>

#include <array>
#include <filesystem>
#include <span>
#include <vector>

namespace hermes::infra {

/**
 * @brief Async double-buffer.
 * 'back_buffer_ready' must be true before swap.
 */
struct DoubleBuffer {
  std::filesystem::path path_;

  /**
   * @brief Flag indicating the async refill operation has completed.
   */
  std::atomic<bool> back_buffer_ready_{false};

  /** @brief Constructor initializes both buffers to BUFFER_SIZE zeros. */
  DoubleBuffer();

  DoubleBuffer(const DoubleBuffer&) = delete;
  DoubleBuffer& operator=(const DoubleBuffer&) = delete;

  /** @brief Returns a span pointing to the currently active read buffer. */
  std::span<uint8_t> GetReadSpan();

  /** @brief Returns a span pointing to the inactive write buffer. */
  std::span<uint8_t> GetWriteSpan();

  /** @brief Sets the index of the read buffer (0 or 1). */
  void SetReadIndex(int value);

  /**
   * @brief Swaps the read and write buffers.
   * @note Ensure 'back_buffer_ready' is true before calling to avoid underrun.
   */
  void Swap();

 private:
  std::array<std::vector<uint8_t>, 2> blocks_;
  int read_index_ = 0;
};
}  // namespace hermes::audio
