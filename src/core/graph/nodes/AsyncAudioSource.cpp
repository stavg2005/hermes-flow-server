#include "AsyncAudioSource.hpp"

#include <spdlog/spdlog.h>

#include <expected>

#include "core/config/Config.hpp"


namespace hermes::audio {

AsyncAudioSource::AsyncAudioSource(boost::asio::io_context& io) : io_(io) {}

boost::asio::awaitable<void> AsyncAudioSource::InitializeBuffers() {
  size_t bytes = co_await FetchBytes(bf_.GetWriteSpan());
  spdlog::debug("got {} bytes", bytes);
  bf_.back_buffer_ready_ = true;

  bf_.Swap();

  bytes = co_await FetchBytes(bf_.GetWriteSpan());
  spdlog::debug("got {} bytes", bytes);
  bf_.back_buffer_ready_ = true;

  co_return;
}

std::expected<void, config::NodeError> AsyncAudioSource::ProcessFrame(
    std::span<uint8_t> frame_buffer) {
  auto current_span = bf_.GetReadSpan();
  size_t buffer_offset = in_buffer_processed_frames_ * config::FRAME_SIZE_BYTES;

  if (processed_frames_ == 0) {
    size_t offset = GetReadOffset(current_span);
    buffer_offset += offset;
  }

  // Check if we need to swap buffers
  if (buffer_offset + config::FRAME_SIZE_BYTES > current_span.size()) {
    // If back buffer isn't ready, we have an underrun (CPU/Disk too slow)
    if (!bf_.back_buffer_ready_) {
      std::fill(frame_buffer.begin(), frame_buffer.end(), 0);
      return Error(config::NodeErrorCode::Underrun, "Underrun in node {}", id_);
    }

    bf_.Swap();

    // Reset counters for the new front buffer
    in_buffer_processed_frames_ = 0;
    current_span = bf_.GetReadSpan();
    buffer_offset = 0;

    boost::asio::co_spawn(
        io_,
        [self = shared_from_this()]() -> boost::asio::awaitable<void> {
          try {
            // Fetch bytes into the write (back) buffer
            co_await self->FetchBytes(self->bf_.GetWriteSpan());
            // Mark ready so the audio thread can swap to it later
            self->bf_.back_buffer_ready_ = true;
          } catch (const std::exception& e) {
            spdlog::error("Async refill failed for node {}: {}", self->id_,
                          e.what());
          }
        },
        boost::asio::detached);
  }

  if (total_frames_ > 0 && processed_frames_ >= total_frames_) {
    std::fill(frame_buffer.begin(), frame_buffer.end(), 0);


    if (processed_frames_ > total_frames_) {
      // Bypass the Error() helper to avoid spdlog calls
      return std::unexpected<config::NodeError>(
          config::NodeErrorCode::EndOfStream);
    }

    // FIRST time hitting EOS: Increment counter and Log it once.
    processed_frames_++;
    return Error(config::NodeErrorCode::EndOfStream, "End of stream for {}",
                 id_);
  }

  auto src_it = current_span.begin() + buffer_offset;

  // Safety check to ensure we don't read past buffer end
  if (std::distance(src_it, current_span.end()) <
      (ptrdiff_t)config::FRAME_SIZE_BYTES) {
    std::fill(frame_buffer.begin(), frame_buffer.end(), 0);
  } else {
    auto src_span = std::span<uint8_t>(src_it, config::FRAME_SIZE_BYTES);

    ApplyEffects(src_span);

    // Copy to output buffer
    std::copy(src_span.begin(), src_span.end(), frame_buffer.begin());
  }

  in_buffer_processed_frames_++;
  processed_frames_++;

  return {};
}

IAudioProcessor* AsyncAudioSource::AsAudio() { return this; };
}  // namespace hermes::audio
