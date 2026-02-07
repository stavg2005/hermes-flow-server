#include "nodes/FileInputNode.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <expected>

#include "FileSink.hpp"
#include "S3Session.hpp"
#include "Types.hpp"
#include "core/config/Config.hpp"
#include "infra/audio/WavUtils.hpp"

using namespace hermes::config;

namespace hermes::audio {

FileInputNode::FileInputNode(boost::asio::io_context& io, std::string name,
                             std::string path)
    : AsyncAudioSource(io),
      file_name_(std::move(name)),
      file_path_(std::move(path)),
      file_handle_(io) {
  kind_ = NodeKind::FileInput;
}

std::expected<void, NodeError> FileInputNode::open() {
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
  return error(NodeErrorCode::FileIOError,
               "Failed to open file {} ({}) after {} attempts.", file_name_,
               file_path_, max_retries);
}

std::expected<void, NodeError> FileInputNode::close() {
  boost::system::error_code ec;
  file_handle_.close(ec);

  if (ec) {
    return error(NodeErrorCode::FileIOError, "Failed to close file {} {}: {}",
                 file_name_, file_path_, ec.message());
  }

  // Reset internal state for potential reuse
  is_first_read_ = true;
  processed_frames_ = 0;
  in_buffer_processed_frames_ = 0;
  return {};
}

boost::asio::awaitable<size_t> FileInputNode::fetch_bytes(
    std::span<uint8_t> dest) {
  if (!file_handle_.is_open()) {
    auto result = open();
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

size_t FileInputNode::get_read_offset(std::span<uint8_t> buffer) {
  // This replaces the logic that was previously inside ProcessFrame
  if (is_first_read_) {
    size_t offset = wav::get_audio_data_offset(buffer);
    is_first_read_ = false;
    return offset;
  }
  return 0;
}

void FileInputNode::apply_effects(std::span<uint8_t> frame_buffer) {
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

boost::asio::awaitable<std::expected<void, config::ErrorInfo>>
FileInputNode::ensure_file_exists() {
  if (std::filesystem::exists(file_path_)) {
    co_return std::expected<void, config::ErrorInfo>{};
  }

  spdlog::info("File missing: {}. Requesting from S3...", file_name_);

  auto session_result = hermes::net::s3::S3Session::create(io_);
  if (!session_result) {
    co_return std::unexpected(config::ErrorInfo::From(
        config::AppError::NetworkError,
        "Failed to create S3Session: " + session_result.error().message));
  }
  std::unique_ptr<hermes::net::s3::S3Session> s3_session(session_result.value());

  infra::FileSink sink(io_);

  if (auto res = sink.Prepare(file_path_); !res) {
    co_return std::unexpected(
        config::ErrorInfo::From(config::AppError::FileSystemError,
                                "Failed to prepare file sink: " + res.error()));
  }

  auto download_result = co_await s3_session->request_file(file_name_, sink);

  if (!download_result) {
    co_return std::unexpected(config::ErrorInfo::From(
        config::AppError::NetworkError,
        "S3 Download failed: " + download_result.error().message));
  }

  sink.Commit();

  spdlog::info("Successfully downloaded: {}", file_name_);
  co_return std::expected<void, config::ErrorInfo>{};
}

void FileInputNode::set_options(FileOptionsNode* options_node) {
  options_ = options_node;
  if (options_) {
    spdlog::info("[{}] Set gain option: {}", file_name_, options_->gain);
  }
}

std::expected<void, config::NodeError> FileInputNode::connect_input(
    Node* source) {
  if (source->kind() == NodeKind::Mixer) {
    return error(config::NodeErrorCode::FormatError,
                 "FileInput cannot receive input from Mixer.");
  }

  if (source->kind() == NodeKind::FileOptions) {
    auto* options = dynamic_cast<FileOptionsNode*>(source);
    set_options(options);
    //  We do NOT call WireStandard here.
    // Options are "side-loaded" config, not an upstream audio source.
    return {};
  }

  if (source->kind() == NodeKind::Delay) {
    wire_standard(source);
    return {};
  }

  return error(config::NodeErrorCode::FormatError,
               "FileInput only accepts Delay or FileOptions.");
}

}  // namespace hermes::audio
