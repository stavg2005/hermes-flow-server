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
  virtual std::expected<void, config::NodeError> process_frame(
      std::span<uint8_t> frame_buffer) = 0;

  /** @brief Release resources and reset node state. */
  virtual std::expected<void, config::NodeError> close() = 0;

  virtual ~IAudioProcessor() = default;
};

/**
 * @brief Interface for nodes that need async initialization.
 */
struct IAsyncInitializer {
  virtual boost::asio::awaitable<void> initialize_buffers() = 0;
  virtual ~IAsyncInitializer() = default;
};

struct Node : public std::enable_shared_from_this<Node> {
 public:
  explicit Node(Node* next = nullptr);
  virtual ~Node() = default;

  [[nodiscard]] std::string_view id() const { return id_; }
  [[nodiscard]] NodeKind kind() const { return kind_; }

  [[nodiscard]] Node* next() const { return target_; }
  void set_next(Node* target) { target_ = target; }  // Explicit rewiring

  int get_total_frames() const { return total_frames_; }
  void set_total_frames(int frames) { total_frames_ = frames; }

  void set_id(std::string id) { id_ = std::move(id); }

  [[nodiscard]] bool is_complete() const {
    return (total_frames_ > 0) && (processed_frames_ >= total_frames_);
  }

  /** * @brief Resets counters. Essential for re-using the graph.
   */
  virtual void reset_state() {
    processed_frames_ = 0;
    in_buffer_processed_frames_ = 0;
  }

  virtual IAudioProcessor* as_audio();
  virtual std::expected<void, config::NodeError> connect_input(Node* source);
  void wire_standard(Node* source);

 protected:
  std::string id_;
  NodeKind kind_;
  Node* target_;

  int processed_frames_{0};
  int total_frames_{0};
  int in_buffer_processed_frames_{0};

  template <typename... Args>
  std::unexpected<config::NodeError> error(config::NodeErrorCode code,
                                           std::format_string<Args...> fmt,
                                           Args&&... args) const {
    std::string msg = std::format(fmt, std::forward<Args>(args)...);
    spdlog::error("[{}] {}", id_, msg);
    return std::unexpected(config::NodeError{code, msg, id_});
  }
};

struct Graph {
  std::vector<std::unique_ptr<Node>> nodes;
  std::unordered_map<std::string, Node*> node_map;
  Node* start_node;
};
}  // namespace hermes::audio
