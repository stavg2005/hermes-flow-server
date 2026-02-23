#pragma once

#include <boost/asio/stream_file.hpp>
#include <cstddef>
#include <memory>
#include <string>

#include "AsyncAudioSource.hpp"
#include "BasicNodes.hpp"

namespace hermes::audio {

/**
 * @brief Specific implementation for Disk Files.
 * Inherits buffering magic from AsyncAudioSource.
 */
struct FileInputNode : public AsyncAudioSource {
  // --- File Specific Members ---
  std::string file_name_;
  std::string file_path_;
  boost::asio::stream_file file_handle_;
  FileOptionsNode* options_ = nullptr;

  // WAV Header Parsing State
  bool is_first_read_ = true;
  size_t header_offset_ = 0;

  explicit FileInputNode(boost::asio::io_context& io, std::string name,
                         std::string path);

  boost::asio::awaitable<std::expected<void, config::ErrorInfo>>
  ensure_file_exists(const config::S3Config& s3_config);

  virtual std::expected<void, config::NodeError> connect_input(
      Node* source) override;

  /**
   * @brief The ONE thing this class must do: fetch bytes from disk.
   * AsyncAudioSource handles the double-buffering logic automatically.
   */
  boost::asio::awaitable<size_t> fetch_bytes(std::span<uint8_t> dest) override;

  /**
   * @brief Optional override to apply gain/options after data is ready.
   */
  void apply_effects(std::span<uint8_t> frame_buffer) override;

  /**
   * @brief Override to handle WAV header skipping on the first buffer.
   */
  size_t get_read_offset(std::span<uint8_t> buffer) override;

  std::expected<void, config::NodeError> open();
  std::expected<void, config::NodeError> close() override;

  /**
   * @brief Link the options node (Gain).
   */
  void set_options(FileOptionsNode* options_node);
};

}  // namespace hermes::audio
