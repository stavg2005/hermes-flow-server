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

using namespace hermes::net::s3;
using namespace hermes::service;

namespace hermes::audio {

AudioExecutor::AudioExecutor(boost::asio::io_context& io, const Graph& graph,
                             config::S3Config& s3_config)
    : io_(io), graph_(graph), s3_config_(std::move(s3_config)) {
  if (graph_.start_node == nullptr) {
    throw std::runtime_error("Invalid graph: missing start node.");
  }
  spdlog::debug("start node in adiou execture {}", graph.start_node->id());
  current_node_ = graph_.start_node;
}

SessionStats& AudioExecutor::get_stats() { return stats_; }

boost::asio::awaitable<std::expected<void, config::ErrorInfo>>
AudioExecutor::prepare() {
  spdlog::info("Preparing Audio Graph...");

  if (current_node_ == nullptr) {
    co_return error(config::AppError::LogicError,
                    "Invalid Graph: No start node");
  }

  auto fetch_res = co_await ensure_assets_exist();
  if (!fetch_res) {
    co_return std::unexpected(fetch_res.error());
  }

  auto init_res = co_await initialize_nodes();
  if (!init_res) {
    co_return std::unexpected(init_res.error());
  }

  update_mixers();

  current_node_ = graph_.start_node;
  spdlog::debug("current node!!!: {}", graph_.start_node->id());
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
  if (current_node_ == nullptr) {
    spdlog::error("current node is null");
    return {false, config::NodeError{config::NodeErrorCode::Success, "", ""}};
  }

  // Clear buffer before processing
  if (output_buffer.empty()) {
    return {false, config::NodeError{config::NodeErrorCode::FormatError,
                                     "Output buffer is empty", ""}};
  }
  std::fill(output_buffer.begin(), output_buffer.end(), 0);

  if (auto* audio_node = current_node_->as_audio()) {
    auto result = audio_node->process_frame(output_buffer);

    if (!result) {
      config::NodeError err = result.error();

      // Non-critical error (Underrun) so we  Log and  Continue
      if (err.code == config::NodeErrorCode::Underrun) {
        return {true, err};
      }

      // Critical error that isn't just "Stream Over"
      if (err.code != config::NodeErrorCode::EndOfStream) {
        return {false, err};
      }
    }

    // Handle Natural End (EOS) or Frame Limit
    bool is_eos =
        (!result && result.error().code == config::NodeErrorCode::EndOfStream);

    ;

    if (is_eos || current_node_->is_complete()) {
      auto close_result = audio_node->close();
      if (!close_result) {
        return {false, close_result.error()};
      }

      spdlog::info(
          "Node [{}] finished. Transitioning to [{}]", current_node_->id(),
          current_node_->next() != nullptr ? current_node_->next()->id()
                                           : "END");

      current_node_ = current_node_->next();

      if (current_node_ != nullptr) {
        stats_.current_node_id = current_node_->id();
      }
    }

    stats_.total_bytes_sent += hermes::config::FRAME_SIZE_BYTES;

    // Return true if we still have a valid node to process next tick
    return {(current_node_ != nullptr),
            config::NodeError{config::NodeErrorCode::Success, "", ""}};
  }

  // Current node is not an audio processor (e.g. Logic node), stop.
  return {false, config::NodeError{config::NodeErrorCode::Success, "", ""}};
}

}  // namespace hermes::audio
