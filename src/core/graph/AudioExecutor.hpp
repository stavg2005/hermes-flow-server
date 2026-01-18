#pragma once

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <memory>
#include <span>
#include <vector>

#include "Config.hpp"
#include "ISessionObserver.hpp"
#include "Node.hpp"
namespace hermes::audio {
/**
 * @brief Executes the audio graph. Handles asset loading and the main
 * processing loop
 */
class AudioExecutor {
 public:
  /**
   * @brief Constructs the executor with a parsed graph.
   * @param io The IO context for async file operations.
   * @param graph The audio graph structure containing nodes and edges.
   */
  AudioExecutor(boost::asio::io_context& io,
                std::shared_ptr<audio::Graph> graph);

  /**
   * @return A reference to the stats object used by Observers (e.g.,
   * WebSocket).
   */
  service::SessionStats& GetStats();

  /**
   * @brief Scans the graph for FileInputNodes.
   * Triggers S3 downloads for any missing files.
   * Pre-fills the initial Double Buffers.
   */
  boost::asio::awaitable<std::expected<void, config::ErrorInfo>> Prepare();

  /**
   * @brief pull data from the current node that is being proccesed
   * @param output_buffer buffer for the mixed PCM audio.
   * @return true if a frame was produced, false if the graph is over .
   */
  std::pair<bool, config::NodeError> GetNextFrame(
      std::span<uint8_t> output_buffer);

 private:
  /**
   * @brief Helper to iterate all nodes and ensure files exist locally.
   * Initiates S3 downloads if files are missing from the disk.
   */
  boost::asio::awaitable<std::expected<void, config::ErrorInfo>> FetchFiles();

  /**
   * @brief Configures mixer nodes based on their inputs.
   * Calculates total frame duration for mixers to know when to stop.
   */
  void UpdateMixers();

  boost::asio::awaitable<std::expected<void, config::ErrorInfo>>
  EnsureAssetsExist();

  boost::asio::awaitable<std::expected<void, config::ErrorInfo>>
  InitializeNodes();
  template <typename... Args>
  std::unexpected<config::ErrorInfo> Error(config::AppError code,
                                           std::format_string<Args...> fmt,
                                           Args&&... args) const {
    std::string msg = std::format(fmt, std::forward<Args>(args)...);
    spdlog::error("[AudioExecutor] {}", msg);
    return std::unexpected(config::ErrorInfo::From(code, msg));
  }
  boost::asio::io_context& io_;
  std::shared_ptr<audio::Graph> graph_;
  Node* current_node_ = nullptr;
  service::SessionStats stats_;
};
};  // namespace hermes::audio
