#include "nodes/FileInputNode.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <expected>

#include "BasicNodes.hpp"
#include "FileSink.hpp"
#include "core/config/Config.hpp"
#include "core/config/Types.hpp"
#include "infra/audio/WavUtils.hpp"
#include "network/s3/S3Session.hpp"  // Corrected header path for S3Session

using namespace hermes::config;

namespace hermes::audio {

FileInputNode::FileInputNode(boost::asio::io_context& io, std::string name,
                             std::string path)
    : file_name_(std::move(name)),
      file_path_(std::move(path)),
      file_handle_(io),
      io_(io) {
  kind_ = NodeKind::FileInput;

  // Initialize the engine component with our fetch_bytes lambda
  buffer_controller_ = std::make_shared<AsyncBufferController>(
      io_, [this](std::span<uint8_t> dest) { return fetch_bytes(dest); });
}

std::expected<void, NodeError> FileInputNode::open() {
  constexpr int max_retries = 3;
  int attempt = 0;
  boost::system::error_code ec;

  while (attempt < max_retries) {
    file_handle_.open(file_path_, boost::asio::file_base::read_only,
                      ec);  // NOLINT

    if (!ec && file_handle_.is_open()) {
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
  file_handle_.close(ec);  // NOLINT

  if (ec) {
    return error(NodeErrorCode::FileIOError, "Failed to close file {} {}: {}",
                 file_name_, file_path_, ec.message());
  }

  // Reset internal state for potential reuse
  is_first_read_ = true;
  processed_frames_ = 0;
  pitch_shifter_.reset();

  // Reset the buffer component
  buffer_controller_->reset();

  if (this->is_in_loop()) {
    spdlog::info("[{}] Node is in a loop. Refilling buffers asynchronously...",
                 file_name_);

    auto self = std::static_pointer_cast<FileInputNode>(shared_from_this());
    boost::asio::co_spawn(
        io_,
        [self]() -> boost::asio::awaitable<void> {
          try {
            co_await self->initialize_buffers();
          } catch (const std::exception& e) {
            spdlog::error("Async loop refill failed for node {}: {}",
                          self->id(), e.what());
          }
        },
        boost::asio::detached);
  }
  return {};
}

// --- NEW DELEGATED METHODS ---

boost::asio::awaitable<void> FileInputNode::initialize_buffers() {
  co_await buffer_controller_->initialize_buffers();
}

std::expected<void, config::NodeError> FileInputNode::process_frame(
    std::span<uint8_t> buffer) {
  // 1. Check for End of Stream
  if (processed_frames_ >= total_frames_ && total_frames_ > 0) {
    std::fill(buffer.begin(), buffer.end(), 0);
    return error(NodeErrorCode::EndOfStream, "End of stream for {}", id_);
  }

  // 2. Fetch data via the controller, handling WAV offset
  size_t offset = get_read_offset(buffer);
  auto result = buffer_controller_->get_frame(buffer, offset);

  if (!result) {
    return result;  // Return Underruns upstream
  }
  // 3. Apply specific File Input DSP (Gain, Pitch)
  apply_effects(buffer);

  processed_frames_++;
  return {};
}

// -----------------------------

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

  spdlog::error("[{}] Failed to read from file after retries.", file_name_);
  co_return 0;
}

size_t FileInputNode::get_read_offset(std::span<uint8_t> buffer) {
  if (is_first_read_) {
    size_t offset = wav::get_audio_data_offset(buffer);
    is_first_read_ = false;
    return offset;
  }
  return 0;
}

void FileInputNode::apply_effects(std::span<uint8_t> frame_buffer) {
  if (options_ == nullptr) { return;
}

  auto* samples = reinterpret_cast<int16_t*>(frame_buffer.data());
  size_t num_samples = frame_buffer.size() / sizeof(int16_t);
  std::span<int16_t> audio_span(samples, num_samples);

  // 1. Apply Gain
  if (options_->gain != 1.0) {
    float gain = options_->gain;
    for (auto& sample : audio_span) {
      int32_t boosted = static_cast<int32_t>(sample * gain);
      sample = static_cast<int16_t>(std::clamp(boosted, -32768, 32767));
    }
  }

  // 2. Apply Pitch Shift (Delegated to the dedicated class)
  if (options_->pitch_shift != 0.0) {
    pitch_shifter_.process(audio_span, options_->pitch_shift);
  }
}

boost::asio::awaitable<std::expected<void, config::ErrorInfo>>
FileInputNode::download_from_s3(const config::S3Config& s3_config) {
  // 1. Create S3 Session
  auto session_result = hermes::net::s3::S3Session::create(io_, s3_config);
  if (!session_result) {
    co_return std::unexpected(session_result.error());
  }

  std::unique_ptr<hermes::net::s3::S3Session> s3_session(
      session_result.value());
  infra::FileSink sink(io_);

  // 2. Prepare local file sink
  if (auto res = sink.Prepare(file_path_); !res) {
    co_return std::unexpected(config::ErrorInfo::From(
        config::AppError::FileSystemError, "Sink Prep Failed: " + res.error()));
  }

  // 3. Execute Streamed Download
  auto download_result = co_await s3_session->request_file(file_name_, sink);
  if (!download_result) {
    // Note: PartialFileGuard handles cleanup automatically here
    co_return std::unexpected(download_result.error());
  }

  // 4. Commit file to disk
  sink.Commit();
  spdlog::info("[{}] Download complete.", file_name_);

  co_return std::expected<void, config::ErrorInfo>{};
}

boost::asio::awaitable<std::expected<void, config::ErrorInfo>>
FileInputNode::ensure_file_exists(const config::S3Config& s3_config) {
  if (std::filesystem::exists(file_path_)) {
    co_return std::expected<void, config::ErrorInfo>{};
  }

  spdlog::info("[{}] File missing, initiating S3 download...", file_name_);

  co_return co_await download_from_s3(s3_config);
}

void FileInputNode::set_options(FileOptionsNode* options_node) {
  options_ = options_node;
  if (options_) {  // NOLINT
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
