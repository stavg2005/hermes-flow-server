#pragma once

#include <boost/asio.hpp>
#include <expected>
#include <memory>
#include <span>

#include "Node.hpp"
#include "core/config/Types.hpp"
#include "infra/audio/DoubleBuffer.hpp"

namespace hermes::audio {

class AsyncAudioSource : public Node,
                         public IAudioProcessor,
                         public IAsyncInitializer {
 protected:
  boost::asio::io_context& io_;
  hermes::infra::DoubleBuffer bf_;
  std::atomic<bool> is_ready_{false};
  /**
   * @brief PURE VIRTUAL: Child must implement how to fetch bytes
   * (Disk/Network).
   * @return Number of bytes actually read.
   */
  virtual boost::asio::awaitable<size_t> fetch_bytes(
      std::span<uint8_t> dest) = 0;

 public:
  explicit AsyncAudioSource(boost::asio::io_context& io);
  virtual ~AsyncAudioSource() = default;

  /**
   * @brief Pre-fills both front and back buffers before playback starts.
   */
  boost::asio::awaitable<void> initialize_buffers() override;

  /**
   * @brief Reads from the buffer. If empty, swaps and triggers async refill.
   */
  std::expected<void, config::NodeError> process_frame(
      std::span<uint8_t> buffer) override;

  IAudioProcessor* as_audio() override;

  virtual void apply_effects(std::span<uint8_t> buffer) {}
  virtual size_t get_read_offset(std::span<uint8_t> buffer) { return 0; }
};

}  // namespace hermes::audio
