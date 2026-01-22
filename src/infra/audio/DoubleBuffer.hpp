

#include <array>
#include <filesystem>
#include <span>
#include <vector>

namespace hermes::infra {

/**
 * @brief Async double-buffer for audio streaming.
 *
 * Designed for a Single-Producer (S3 Loader), Single-Consumer (Audio Thread) model.
 * - The "Back" buffer is filled asynchronously.
 * - The "Front" (Read) buffer is consumed by the audio engine.
 * - 'back_buffer_ready_' signals when the back buffer is full and ready to swap.
 */
struct DoubleBuffer {
  std::filesystem::path path_;

  /**
   * @brief Atomic flag indicating the back buffer is full.
   *
   * Set to 'true' by the S3 loader when download completes for a chunk.
   * Checked and cleared by the Audio Executor during the swap phase.
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
