#include "core/graph/AudioExecutor.hpp"

#include <algorithm>
#include <expected>
#include <filesystem>
#include <vector>

#include "FileSink.hpp"
#include "Nodes.hpp"
#include "Types.hpp"
#include "core/graph/Node.hpp"
#include "network/s3/S3Session.hpp"
#include "spdlog/spdlog.h"
#include "unordered_set"
using namespace hermes::net::s3;
using namespace hermes::service;

namespace hermes::audio {

AudioExecutor::AudioExecutor(boost::asio::io_context& io, const Graph& graph,
                             config::S3Config s3_config)
    : io_(io), graph_(graph), s3_config_(std::move(s3_config)) {
  if (graph_.start_node == nullptr) {
    throw std::runtime_error("Invalid graph: missing start node.");
  }
  current_node_ = graph_.start_node;
}

SessionStats& AudioExecutor::get_stats() { return stats_; }

void AudioExecutor::detect_and_flag_loops() const {
  Node* curr = graph_.start_node;
  std::unordered_set<Node*> visited;
  Node* cycle_start = nullptr;

  // 1. Traverse and detect the cycle
  while (curr != nullptr) {
    if (!visited.insert(curr).second) {
      cycle_start = curr;  // Found the start of the cycle
      break;
    }
    curr = curr->next();
  }

  // 2. Flag the nodes participating in the cycle
  if (cycle_start != nullptr) {
    spdlog::info("Loop detected in graph! Flagging loop nodes...");
    Node* loop_node = cycle_start;
    do {
      loop_node->set_in_loop(true);
      loop_node = loop_node->next();
    } while (loop_node != cycle_start);
  }
}

boost::asio::awaitable<std::expected<void, config::ErrorInfo>>
AudioExecutor::prepare() {
  spdlog::info("Preparing Audio Graph...");

  if (current_node_ == nullptr) {
    co_return error(config::AppError::LogicError,
                    "Invalid Graph: No start node");
  }

  // Step 1: Fetch remote files if needed
  auto fetch_res = co_await ensure_assets_exist();
  if (!fetch_res) {
    co_return std::unexpected(fetch_res.error());
  }

  // Step 2: Pre-fill buffers
  auto init_res = co_await initialize_nodes();
  if (!init_res) {
    co_return std::unexpected(init_res.error());
  }

  // Step 3: Configure mixer durations
  update_mixers();

  // Step 4: Detect loops in graph
  detect_and_flag_loops();

  // Step 5: Finalize session state
  current_node_ = graph_.start_node;
  stats_.current_node_id = current_node_->id();
  stats_.total_bytes_sent = 0;

  co_return std::expected<void, config::ErrorInfo>();
}

boost::asio::awaitable<std::expected<void, config::ErrorInfo>>
AudioExecutor::ensure_assets_exist() {
  spdlog::info("Checking asset availability...");

  for (const auto& node : graph_.nodes) {
    if (node->kind() != NodeKind::FileInput) {
      continue;
    }

    auto* file_node = static_cast<FileInputNode*>(node.get());
    auto results = co_await file_node->ensure_file_exists(s3_config_);
    if (!results) {
      co_return std::unexpected<config::ErrorInfo>(results.error());
    }
  }

  co_return std::expected<void, config::ErrorInfo>();
}

boost::asio::awaitable<std::expected<void, config::ErrorInfo>>
AudioExecutor::initialize_nodes() {
  spdlog::info("Initializing async nodes...");

  for (const auto& node : graph_.nodes) {
    // Check if this node implements IAsyncInitializer (e.g. AsyncAudioSource)
    if (auto* async_node = dynamic_cast<IAsyncInitializer*>(node.get())) {
      spdlog::debug("Initializing buffers for node [{}]", node->id());
      co_await async_node->initialize_buffers();
    }
  }
  co_return std::expected<void, config::ErrorInfo>();
}

void AudioExecutor::update_mixers() {
  for (const auto& node : graph_.nodes) {
    if (node->kind() == NodeKind::Mixer) {
      if (auto* mixer = dynamic_cast<MixerNode*>(node.get())) {
        mixer->set_max_frames();
      }
    }
  }
}
std::pair<bool, config::NodeError> AudioExecutor::get_next_frame(
    std::span<uint8_t> output_buffer) {
  if (current_node_ == nullptr || output_buffer.empty()) {
    spdlog::error(
        "Invalid state: current node is null or output buffer is empty");
    return {false, config::NodeError{config::NodeErrorCode::FormatError,
                                     "Invalid state or buffer", ""}};
  }

  std::fill(output_buffer.begin(), output_buffer.end(), 0);

  auto* audio_node = current_node_->as_audio();
  if (!audio_node) {
    // Current node is not an audio processor (e.g. Logic node), stop.
    return {false, config::NodeError{config::NodeErrorCode::Success, "", ""}};
  }

  auto result = audio_node->process_frame(output_buffer);

  // 1. Handle Active Errors (Ignore EndOfStream as it's a lifecycle event)
  if (!result && result.error().code != config::NodeErrorCode::EndOfStream) {
    // Underruns are non-critical, we tell the caller to continue (true)
    bool is_recoverable =
        (result.error().code == config::NodeErrorCode::Underrun);
    return {is_recoverable, result.error()};
  }

  // 2. Handle Node Lifecycle / Graph Traversal
  bool is_eos =
      (!result && result.error().code == config::NodeErrorCode::EndOfStream);
  if (is_eos || current_node_->is_complete()) {
    auto transition_result = advance_to_next_node();
    if (!transition_result) {
      return {false, transition_result.error()};
    }
  }

  stats_.total_bytes_sent += hermes::config::FRAME_SIZE_BYTES;

  return {(current_node_ != nullptr),
          config::NodeError{config::NodeErrorCode::Success, "", ""}};
}

std::expected<void, config::NodeError> AudioExecutor::advance_to_next_node() {
  if (auto* audio_node = current_node_->as_audio()) {
    auto close_result = audio_node->close();
    if (!close_result) {
      return std::unexpected(close_result.error());
    }
  }

  spdlog::info(
      "Node [{}] finished. Transitioning to [{}]", current_node_->id(),
      current_node_->next() != nullptr ? current_node_->next()->id() : "END");

  current_node_ = current_node_->next();

  if (current_node_ != nullptr) {
    stats_.current_node_id = current_node_->id();
  }

  return {};
}

}  // namespace hermes::audio
