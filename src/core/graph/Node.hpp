#pragma once
#include <spdlog/spdlog.h>
#include <stdlib.h>

#include <Types.hpp>
#include <expected>
#include <span>

namespace hermes::audio {
constexpr int RETRY_DELAY_MS = 50;
static constexpr int max_16_bytes = 32767;
static constexpr int min_16_bytes = -32767;
/**
 * @brief Type of node in the audio processing graph.
 */
enum class NodeKind { FileInput, Mixer, Delay, Clients, FileOptions };

/**
 * @brief Interface for nodes that can produce audio frames.
 */
struct IAudioProcessor {
  /** @brief Process the next audio frame into the provided buffer. */
  virtual std::expected<void, config::NodeError> ProcessFrame(
      std::span<uint8_t> frame_buffer) = 0;

  /** @brief Release resources and reset node state. */
  virtual std::expected<void, config::NodeError> Close() = 0;

  virtual ~IAudioProcessor() = default;
};

/**
 * @brief Interface for nodes that need async initialization.
 */
struct IAsyncInitializer {
  virtual boost::asio::awaitable<void> InitializeBuffers() = 0;
  virtual ~IAsyncInitializer() = default;
};

// Base Node

/**
 * @brief Base class for all nodes in the audio graph
 * Holds execution state and links to the next node.
 */
struct Node {
  std::string id_;
  NodeKind kind_;
  Node* target_ = nullptr;

  // Execution State (Managed by AudioExecutor)
  int processed_frames_{0};     /**< Number of 20ms frames processed so far */
  int total_frames_{0};         /**< Total duration in 20ms frames (e.g. file size / frame_size) */
  int in_buffer_processed_frames_{0}; /**< Offset within the current audio buffer (in frames) */

  explicit Node(Node* t = nullptr);
  virtual ~Node() = default;

  /** @brief Returns this node as an audio processor, if it is. */
  virtual IAudioProcessor* AsAudio();

 protected:
  template <typename... Args>
  std::unexpected<config::NodeError> Error(config::NodeErrorCode code,
                                           std::format_string<Args...> fmt,
                                           Args&&... args) const {
    std::string msg = std::format(fmt, std::forward<Args>(args)...);

    spdlog::error("[{}] {}", id_, msg);

    return std::unexpected(config::NodeError{code, msg, id_});
  }
};

struct Graph {
  std::vector<std::shared_ptr<Node>> nodes;
  std::unordered_map<std::string, std::shared_ptr<Node>> node_map;
  Node* start_node = nullptr;
};
}  // namespace hermes::audio
