#include "AsyncBufferController.hpp"

#include <spdlog/spdlog.h>
#include <algorithm>

namespace hermes::audio {

AsyncBufferController::AsyncBufferController(boost::asio::io_context& io, FetchCallback fetch_cb)
    : io_(io), fetch_cb_(std::move(fetch_cb)) {}

boost::asio::awaitable<void> AsyncBufferController::initialize_buffers() {
  state_ = BufferState::Initializing;

  auto span1 = bf_.get_write_span();
  size_t bytes1 = co_await fetch_cb_(span1);
  if (bytes1 < span1.size()) {
    std::fill(span1.begin() + bytes1, span1.end(), 0);
  }
  bf_.back_buffer_ready_ = true;
  bf_.swap();

  auto span2 = bf_.get_write_span();
  size_t bytes2 = co_await fetch_cb_(span2);
  if (bytes2 < span2.size()) {
    std::fill(span2.begin() + bytes2, span2.end(), 0);
  }
  bf_.back_buffer_ready_ = true;

  state_ = BufferState::Ready;
}

std::expected<void, config::NodeError> AsyncBufferController::get_frame(
    std::span<uint8_t> output_buffer, size_t offset) {

  auto current_span = bf_.get_read_span();
  size_t buffer_offset = (in_buffer_processed_frames_ * config::FRAME_SIZE_BYTES) + offset;

  // Do we need to swap?
  if (buffer_offset + config::FRAME_SIZE_BYTES > current_span.size()) {
    if (!bf_.back_buffer_ready_) {
      state_ = BufferState::Underrun;
      std::fill(output_buffer.begin(), output_buffer.end(), 0);
      return std::unexpected(config::NodeError{config::NodeErrorCode::Underrun, "Buffer underrun", ""});
    }

    swap_and_trigger_refill();
    current_span = bf_.get_read_span();
    buffer_offset = offset; // Reset offset for the new buffer
  }

  // Copy data to output
  auto src_it = current_span.begin() + buffer_offset;
  std::copy(src_it, src_it + config::FRAME_SIZE_BYTES, output_buffer.begin());

  in_buffer_processed_frames_++;
  return {};
}

void AsyncBufferController::reset() {
  bf_.reset();
  in_buffer_processed_frames_ = 0;
  state_ = BufferState::Idle;
}

void AsyncBufferController::swap_and_trigger_refill() {
  bf_.swap();
  in_buffer_processed_frames_ = 0;

  auto self = shared_from_this();
  boost::asio::co_spawn(
      io_,
      [self]() -> boost::asio::awaitable<void> {
        try {
          auto write_span = self->bf_.get_write_span();
          size_t bytes_read = co_await self->fetch_cb_(write_span);

          if (bytes_read < write_span.size()) {
            std::fill(write_span.begin() + bytes_read, write_span.end(), 0);
            if (bytes_read == 0) {
              self->state_ = BufferState::EndOfStream;
            }
          }
          self->bf_.back_buffer_ready_ = true;
        } catch (const std::exception& e) {
          spdlog::error("Refill failed: {}", e.what());
          self->state_ = BufferState::Faulted;
        }
      },
      boost::asio::detached);
}

}  // namespace hermes::audio
