#pragma once

#include <atomic>
#include <boost/asio.hpp>
#include <expected>
#include <functional>
#include <span>

#include "core/config/Config.hpp"
#include "core/config/Types.hpp"
#include "infra/audio/DoubleBuffer.hpp"

namespace hermes::audio {

enum class BufferState { Idle, Initializing, Ready, Underrun, Faulted, EndOfStream };

class AsyncBufferController : public std::enable_shared_from_this<AsyncBufferController> {
 public:
  // Callback returns the number of bytes read
  using FetchCallback = std::function<boost::asio::awaitable<size_t>(std::span<uint8_t>)>;

  AsyncBufferController(boost::asio::io_context& io, FetchCallback fetch_cb);

  /**
   * @brief Pre-fills both front and back buffers before playback starts.
   */
  boost::asio::awaitable<void> initialize_buffers();

  /**
   * @brief Pulls data from the double buffer, triggering a background swap if needed.
   */
  std::expected<void, config::NodeError> get_frame(std::span<uint8_t> output_buffer, size_t offset = 0);

  /**
   * @brief Resets the internal buffer state and processed frames count.
   */
  void reset();

  BufferState get_state() const { return state_.load(); }

 private:
  void swap_and_trigger_refill();

  boost::asio::io_context& io_;
  infra::DoubleBuffer bf_;
  FetchCallback fetch_cb_;
  size_t in_buffer_processed_frames_ = 0;
  std::atomic<BufferState> state_{BufferState::Idle};
};

}  // namespace hermes::audio
