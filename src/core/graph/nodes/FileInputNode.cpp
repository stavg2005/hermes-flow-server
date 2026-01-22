#include "nodes/FileInputNode.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>

#include "core/config/Config.hpp"
#include "infra/audio/WavUtils.hpp"

using namespace hermes::config;

namespace hermes::audio {

// =========================================================
//  Constructor & Lifecycle
// =========================================================

FileInputNode::FileInputNode(boost::asio::io_context& io, std::string name,
                             std::string path)
    : AsyncAudioSource(io),
      file_name_(std::move(name)),
      file_path_(std::move(path)),
      file_handle_(io) {
  kind_ = NodeKind::FileInput;
}

std::expected<void, NodeError> FileInputNode::Open() {
  constexpr int max_retries = 3;
  int attempt = 0;
  boost::system::error_code ec;

  while (attempt < max_retries) {
    file_handle_.open(file_path_, boost::asio::file_base::read_only, ec);

    if (!ec && file_handle_.is_open()) {
      // Set total_frames_ in the base Node class
      total_frames_ = static_cast<int>(file_handle_.size() / FRAME_SIZE_BYTES);
      spdlog::info("[{}] Opened file. Total frames: {}", file_name_,
                   total_frames_);
      return {};  // Success
    }

    spdlog::warn("[{}] Attempt {}: Failed to open file {}: {}", file_name_,
                 attempt + 1, file_path_, ec.message());
    attempt++;
  }

  total_frames_ = 0;
  return Error(NodeErrorCode::FileIOError,
               "Failed to open file {} ({}) after {} attempts.", file_name_,
               file_path_, max_retries);
}

std::expected<void, NodeError> FileInputNode::Close() {
  boost::system::error_code ec;
  file_handle_.close(ec);
  
  if (ec) {
    return Error(NodeErrorCode::FileIOError, "Failed to close file {} {}: {}",
                 file_name_, file_path_, ec.message());
  }

  // Reset internal state for potential reuse
  is_first_read_ = true;
  processed_frames_ = 0;
  in_buffer_processed_frames_ = 0;
  return {};
}

boost::asio::awaitable<size_t> FileInputNode::FetchBytes(
    std::span<uint8_t> dest) {
  if (!file_handle_.is_open()) {
    auto result = Open();
    if (!result) {
      spdlog::error("[{}] Failed to lazy-open file: {}", file_name_,
                    result.error().message);
      co_return 0;
    }
  }

  constexpr int max_retries = 3;
  int attempt = 0;

  while (attempt < max_retries) {
    auto [ec, bytes_read] = co_await boost::asio::async_read(
        file_handle_, boost::asio::buffer(dest),
        boost::asio::as_tuple(boost::asio::use_awaitable));

    if (!ec || ec == boost::asio::error::eof) {
      // Base class handles zero-filling if bytes_read < dest.size()
      co_return bytes_read;
    } else {
      spdlog::warn("[{}] Read attempt {} failed: {}", file_name_, attempt + 1,
                   ec.message());
      attempt++;

      boost::asio::steady_timer timer(file_handle_.get_executor());
      timer.expires_after(std::chrono::milliseconds(RETRY_DELAY_MS));
      co_await timer.async_wait(boost::asio::use_awaitable);
    }
  }

  // Exhausted retries
  spdlog::error("[{}] Failed to read from file after retries.", file_name_);
  co_return 0;
}

size_t FileInputNode::GetReadOffset(std::span<uint8_t> buffer) {
  // This replaces the logic that was previously inside ProcessFrame
  if (is_first_read_) {
    size_t offset = wav::GetAudioDataOffset(buffer);
    is_first_read_ = false;
    return offset;
  }
  return 0;
}

void FileInputNode::ApplyEffects(std::span<uint8_t> frame_buffer) {
  if (!options_ || options_->gain == 1.0) return;

  // Note: ideally check frame_buffer.size() alignment here
  auto gain = options_->gain;
  auto* samples = reinterpret_cast<int16_t*>(frame_buffer.data());
  size_t count = frame_buffer.size() / sizeof(int16_t);

  for (size_t i = 0; i < count; i++) {
    int32_t current_sample = samples[i];
    auto boosted = static_cast<int32_t>(current_sample * gain);
    samples[i] =
        static_cast<int16_t>(std::clamp(boosted, min_16_bytes, max_16_bytes));
  }
}

// =========================================================
//  Options & Configuration
// =========================================================

void FileInputNode::SetOptions(std::shared_ptr<FileOptionsNode> options_node) {
  options_ = std::move(options_node);
  if (options_) {
    spdlog::info("[{}] Set gain option: {}", file_name_, options_->gain);
  }
}

}  // namespace hermes::audio
