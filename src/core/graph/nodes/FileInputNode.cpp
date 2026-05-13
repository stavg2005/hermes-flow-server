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

  buffer_controller_ = std::make_shared<AsyncBufferController>(
      io_, [this](std::span<uint8_t> dest) { return fetch_bytes(dest); });
}

std::expected<void, NodeError> FileInputNode::open() {
  constexpr int max_retries = 3;
  int attempt = 0;
  boost::system::error_code ec;

  while (attempt < max_retries) {
    file_handle_.open(file_path_, boost::asio::file_base::read_only,// NOLINT(bugprone-unused-return-value)
                      ec);

    if (!ec && file_handle_.is_open()) {
      constexpr size_t HEADER_BUF_SIZE = 256;
      std::array<uint8_t, HEADER_BUF_SIZE> header_buf;
      size_t bytes_read = file_handle_.read_some(boost::asio::buffer(header_buf), ec);

      size_t offset = 0;
      if (!ec && bytes_read > 0) {
        offset = wav::get_audio_data_offset(std::span<uint8_t>(header_buf.data(), bytes_read));
      }

      file_handle_.seek(static_cast<int64_t>(offset), boost::asio::file_base::seek_set, ec);
      if (ec) {
        spdlog::warn("[{}] Failed to seek file past header: {}", file_name_, ec.message());
        offset = 0;
        file_handle_.seek(0, boost::asio::file_base::seek_set, ec);
      }

      uint64_t file_size = file_handle_.size(ec);
      total_frames_ = static_cast<int>((file_size > static_cast<uint64_t>(offset) ? file_size - static_cast<uint64_t>(offset) : 0ULL) / FRAME_SIZE_BYTES);

      spdlog::info("[{}] Opened file. Offset: {}, Total frames: {}", file_name_, offset, total_frames_);
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
  spdlog::info("closed file input {}", file_name_);
  // Reset internal state for potential reuse
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

void FileInputNode::set_in_loop(bool val) { is_in_loop_ = val; }

boost::asio::awaitable<void> FileInputNode::initialize_buffers() {
  co_await buffer_controller_->initialize_buffers();
}

std::expected<void, config::NodeError> FileInputNode::process_frame(
    std::span<uint8_t> buffer) {
  if (processed_frames_ >= total_frames_ && total_frames_ > 0) {
    std::fill(buffer.begin(), buffer.end(), 0);
    return error(NodeErrorCode::EndOfStream, "End of stream for {}", id_);
  }

  auto result = buffer_controller_->get_frame(buffer, 0);

  if (!result) {
    return result;
  }
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



void FileInputNode::apply_effects(std::span<uint8_t> frame_buffer) {
  if (options_ == nullptr) {
    return;
  }

  auto* samples = reinterpret_cast<int16_t*>(frame_buffer.data());
  size_t num_samples = frame_buffer.size() / sizeof(int16_t);
  std::span<int16_t> audio_span(samples, num_samples);

  if (options_->gain != 1.0) {
    float gain = static_cast<float>(options_->gain);
    constexpr int32_t MIN_INT16 = -32768;
    constexpr int32_t MAX_INT16 = 32767;
    for (auto& sample : audio_span) {
      int32_t boosted = static_cast<int32_t>(static_cast<float>(sample) * gain);
      sample = static_cast<int16_t>(std::clamp(boosted, MIN_INT16, MAX_INT16));
    }
  }

  if (options_->pitch_shift != 0.0) {
    pitch_shifter_.process(audio_span, static_cast<float>(options_->pitch_shift));
  }
}

boost::asio::awaitable<std::expected<void, config::ErrorInfo>>
FileInputNode::download_from_s3(const config::S3Config& s3_config) {
  auto session_result = hermes::net::s3::S3Session::create(io_, s3_config);
  if (!session_result) {
    co_return std::unexpected(session_result.error());
  }

  std::unique_ptr<hermes::net::s3::S3Session> s3_session(
      session_result.value());
  infra::FileSink sink(io_);

  if (auto res = sink.Prepare(file_path_); !res) {
    co_return std::unexpected(config::ErrorInfo::From(
        config::AppError::FileSystemError, "Sink Prep Failed: " + res.error()));
  }

  auto download_result = co_await s3_session->request_file(file_name_, sink);
  if (!download_result) {
    // Note: PartialFileGuard handles cleanup automatically here
    co_return std::unexpected(download_result.error());
  }

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
