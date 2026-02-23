#include "AsyncAudioSource.hpp"

#include <spdlog/spdlog.h>

#include <expected>

#include "core/config/Config.hpp"

namespace hermes::audio {

AsyncAudioSource::AsyncAudioSource(boost::asio::io_context& io) : io_(io) {}

boost::asio::awaitable<void> AsyncAudioSource::initialize_buffers() {
  auto span1 = bf_.get_write_span();
  size_t bytes1 = co_await fetch_bytes(span1);
  if (bytes1 < span1.size()) {
    std::fill(span1.begin() + bytes1, span1.end(), 0);
  }
  bf_.back_buffer_ready_ = true;
  bf_.swap();

  auto span2 = bf_.get_write_span();
  size_t bytes2 = co_await fetch_bytes(span2);
  if (bytes2 < span2.size()) {
    std::fill(span2.begin() + bytes2, span2.end(), 0);
  }
  bf_.back_buffer_ready_ = true;

  co_return;
}

std::expected<void, config::NodeError> AsyncAudioSource::process_frame(
    std::span<uint8_t> frame_buffer) {
  auto current_span = bf_.get_read_span();
  size_t buffer_offset = in_buffer_processed_frames_ * config::FRAME_SIZE_BYTES;

  if (processed_frames_ == 0) {
    size_t offset = get_read_offset(current_span);
    buffer_offset += offset;
  }

  // Check if we need to swap buffers
  if (buffer_offset + config::FRAME_SIZE_BYTES > current_span.size()) {
    // If back buffer isn't ready, we have an underrun (CPU/Disk too slow)
    if (!bf_.back_buffer_ready_) {
      std::fill(frame_buffer.begin(), frame_buffer.end(), 0);
      return error(config::NodeErrorCode::Underrun, "Underrun in node {}", id_);
    }

    bf_.swap();

    // Reset counters for the new front buffer
    in_buffer_processed_frames_ = 0;
    current_span = bf_.get_read_span();
    buffer_offset = 0;
    // to avoid use after free if the seessions disconnects while we refill
    auto self = std::static_pointer_cast<AsyncAudioSource>(shared_from_this());
    boost::asio::co_spawn(
        io_,
        [self]() -> boost::asio::awaitable<void> {
          try {
            auto write_span = self->bf_.get_write_span();

            size_t bytes_read = co_await self->fetch_bytes(write_span);

            if (bytes_read < write_span.size()) {
              std::fill(write_span.begin() + bytes_read, write_span.end(), 0);
            }
            // Mark ready so the audio thread can swap to it later
            self->bf_.back_buffer_ready_ = true;
          } catch (const std::exception& e) {
            spdlog::error("Async refill failed for node {}: {}", self->id_,
                          e.what());
          }
        },
        boost::asio::detached);
  }

  // when file is in mixer with longer files the mixer would request frames even
  // tough the file input has finished so we will just return zeros (silence)
  if (total_frames_ > 0 && processed_frames_ >= total_frames_) {
    std::fill(frame_buffer.begin(), frame_buffer.end(), 0);

    if (processed_frames_ > total_frames_) {
      // Bypass the Error() helper to avoid spdlog calls
      return std::unexpected<config::NodeError>(
          config::NodeErrorCode::EndOfStream);
    }

    // FIRST time hitting EOS: Increment counter and Log it once.
    processed_frames_++;
    return error(config::NodeErrorCode::EndOfStream, "End of stream for {}",
                 id_);
  }

  auto src_it = current_span.begin() + buffer_offset;

  // Safety check to ensure we don't read past buffer end
  if (std::distance(src_it, current_span.end()) <
      (ptrdiff_t)config::FRAME_SIZE_BYTES) {
    std::fill(frame_buffer.begin(), frame_buffer.end(), 0);
  } else {
    auto src_span = std::span<uint8_t>(src_it, config::FRAME_SIZE_BYTES);

    apply_effects(src_span);

    std::copy(src_span.begin(), src_span.end(), frame_buffer.begin());
  }

  in_buffer_processed_frames_++;
  processed_frames_++;

  return {};
}

IAudioProcessor* AsyncAudioSource::as_audio() { return this; };
}  // namespace hermes::audio
