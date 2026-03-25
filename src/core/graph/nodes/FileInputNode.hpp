#pragma once

#include <array>
#include <boost/asio/stream_file.hpp>
#include <cstddef>
#include <memory>
#include <string>
#include <span>
#include <expected>

#include "AsyncBufferController.hpp" // Replaced AsyncAudioSource
#include "BasicNodes.hpp"
#include "PitchShifter.h"
#include "core/config/Types.hpp"

namespace hermes::audio {

/**
 * @brief Specific implementation for Disk Files.
 * Inherits buffering magic from AsyncBufferController via composition.
 */
struct FileInputNode : public Node, public IAudioProcessor, public IAsyncInitializer {
  // --- File Specific Members ---
  std::string file_name_;
  std::string file_path_;
  boost::asio::stream_file file_handle_;
  FileOptionsNode* options_ = nullptr;
  PitchShifter pitch_shifter_;

  // WAV Header Parsing State
  bool is_first_read_ = true;
  size_t header_offset_ = 0;

  // Frame Tracking (Moved from old AsyncAudioSource base)
  int total_frames_ = 0;
  int processed_frames_ = 0;

  explicit FileInputNode(boost::asio::io_context& io, std::string name,
                         std::string path);

  boost::asio::awaitable<std::expected<void, config::ErrorInfo>>
  ensure_file_exists(const config::S3Config& s3_config);

  /**
   * @brief Internal helper to handle the S3 download logic.
   * Separates infrastructure (networking) from audio domain logic.
   */
  boost::asio::awaitable<std::expected<void, config::ErrorInfo>>
  download_from_s3(const config::S3Config& s3_config);

  virtual std::expected<void, config::NodeError> connect_input(
      Node* source) override;

  /**
   * @brief The ONE thing this class must do: fetch bytes from disk.
   * AsyncBufferController handles the double-buffering logic automatically.
   */
  boost::asio::awaitable<size_t> fetch_bytes(std::span<uint8_t> dest);

  /**
   * @brief Optional override to apply gain/options after data is ready.
   */
  void apply_effects(std::span<uint8_t> frame_buffer);

  /**
   * @brief Override to handle WAV header skipping on the first buffer.
   */
  size_t get_read_offset(std::span<uint8_t> buffer);

  std::expected<void, config::NodeError> open();
  std::expected<void, config::NodeError> close() override;

  /**
   * @brief Link the options node (Gain).
   */
  void set_options(FileOptionsNode* options_node);

  // --- IAudioProcessor / IAsyncInitializer Implementations ---
  boost::asio::awaitable<void> initialize_buffers() override;
  std::expected<void, config::NodeError> process_frame(std::span<uint8_t> buffer) override;
  IAudioProcessor* as_audio() override { return this; }

 private:
  boost::asio::io_context& io_;
  std::shared_ptr<AsyncBufferController> buffer_controller_;
};

}  // namespace hermes::audio
