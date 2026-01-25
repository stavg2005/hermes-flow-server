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

  /**
   * @brief PURE VIRTUAL: Child must implement how to fetch bytes
   * (Disk/Network).
   * @return Number of bytes actually read.
   */
  virtual boost::asio::awaitable<size_t> FetchBytes(
      std::span<uint8_t> dest) = 0;

 public:
  explicit AsyncAudioSource(boost::asio::io_context& io);
  virtual ~AsyncAudioSource() = default;

  // --- IAsyncInitializer Implementation ---
  /**
   * @brief Pre-fills both front and back buffers before playback starts.
   */
  boost::asio::awaitable<void> InitializeBuffers() override;

  // --- IAudioProcessor Implementation ---
  /**
   * @brief Reads from the buffer. If empty, swaps and triggers async refill.
   */
  std::expected<void, config::NodeError> ProcessFrame(
      std::span<uint8_t> buffer) override;

  IAudioProcessor* AsAudio() override;

  // --- Optional Hooks ---
  virtual void ApplyEffects(std::span<uint8_t> buffer) {}
  virtual size_t GetReadOffset(std::span<uint8_t> buffer) { return 0; }
};

}  // namespace hermes::audio
