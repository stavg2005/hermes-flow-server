#pragma once
#include <spdlog/spdlog.h>
#include <stdlib.h>

#include <Types.hpp>
#include <expected>
#include <memory>
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
struct Node : public std::enable_shared_from_this<Node> {
 public:
  explicit Node(Node* next = nullptr);
  virtual ~Node() = default;

  [[nodiscard]] std::string_view Id() const { return id_; }
  [[nodiscard]] NodeKind Kind() const { return kind_; }

  [[nodiscard]] std::shared_ptr<Node> Next() const { return target_.lock(); }
  void SetNext(std::shared_ptr<Node> target) {
    target_ = target;
  }  // Explicit rewiring

  int GetTotalFrames() const { return total_frames_; }
  void SetTotalFreames(int frames) { total_frames_ = frames; }

  void SetId(std::string id) { id_ = std::move(id); }

  [[nodiscard]] bool IsComplete() const {
    return (total_frames_ > 0) && (processed_frames_ >= total_frames_);
  }

  /** * @brief Resets counters. Essential for re-using the graph.
   */
  virtual void ResetState() {
    processed_frames_ = 0;
    in_buffer_processed_frames_ = 0;
  }

  // --- 3. Polymorphic Interfaces ---
  virtual IAudioProcessor* AsAudio();
  virtual std::expected<void, config::NodeError> ConnectInput(
      std::shared_ptr<Node> source);
  void WireStandard(std::shared_ptr<Node> source);

 protected:
  // Protected: Subclasses (Mixer, FileInput) need direct access for
  // performance
  std::string id_;
  NodeKind kind_;
  std::weak_ptr<Node> target_;

  int processed_frames_{0};
  int total_frames_{0};
  int in_buffer_processed_frames_{0};

  // Helper for error logging
  template <typename... Args>
  std::unexpected<config::NodeError> Error(config::NodeErrorCode code,
                                           std::format_string<Args...> fmt,
                                           Args&&... args) const {
    // ... implementation same as before ...
    std::string msg = std::format(fmt, std::forward<Args>(args)...);
    spdlog::error("[{}] {}", id_, msg);
    return std::unexpected(config::NodeError{code, msg, id_});
  }
};

struct Graph {
  std::vector<std::shared_ptr<Node>> nodes;
  std::unordered_map<std::string, std::shared_ptr<Node>> node_map;
  std::weak_ptr<Node> start_node;
};
}  // namespace hermes::audio
