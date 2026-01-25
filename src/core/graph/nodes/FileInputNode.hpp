#pragma once

#include <boost/asio/stream_file.hpp>
#include <memory>
#include <string>

#include "AsyncAudioSource.hpp"

// Forward declaration if needed, or include the header
#include "nodes/FileOptionsNode.hpp"

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
  std::shared_ptr<FileOptionsNode> options_;

  // WAV Header Parsing State
  bool is_first_read_ = true;
  size_t header_offset_ = 0;

  explicit FileInputNode(boost::asio::io_context& io, std::string name,
                         std::string path);

  // --- AsyncAudioSource Implementations ---

  virtual std::expected<void, config::NodeError> ConnectInput(
      std::shared_ptr<Node> source) override;

  /**
   * @brief The ONE thing this class must do: fetch bytes from disk.
   * AsyncAudioSource handles the double-buffering logic automatically.
   */
  boost::asio::awaitable<size_t> FetchBytes(std::span<uint8_t> dest) override;

  /**
   * @brief Optional override to apply gain/options after data is ready.
   */
  void ApplyEffects(std::span<uint8_t> frame_buffer) override;

  /**
   * @brief Override to handle WAV header skipping on the first buffer.
   */
  size_t GetReadOffset(std::span<uint8_t> buffer) override;

  // --- Lifecycle Overrides ---

  std::expected<void, config::NodeError> Open();
  std::expected<void, config::NodeError> Close() override;

  /**
   * @brief Link the options node (Gain).
   */
  void SetOptions(std::shared_ptr<FileOptionsNode> options_node);
};

}  // namespace hermes::audio
