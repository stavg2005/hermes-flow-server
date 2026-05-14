#include "core/graph/AudioExecutor.hpp"

#include <algorithm>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <expected>
#include <vector>

#include "Nodes.hpp"
#include "Types.hpp"

namespace {
/**
 * @brief Helper to dynamically execute multiple awaitables in parallel safely.
 */
template <typename T, typename AsyncFunc>
boost::asio::awaitable<std::expected<void, hermes::config::ErrorInfo>>
await_all_dynamic(boost::asio::io_context& io, const std::vector<T*>& items,
                  AsyncFunc async_func) {  // FIX 1: Pass by value, not &&

  if (items.empty()) {
    co_return std::expected<void, hermes::config::ErrorInfo>();
  }

  using ChannelType = boost::asio::experimental::channel<void(
      boost::system::error_code,
      std::expected<void, hermes::config::ErrorInfo>)>;

  // FIX 2: Heap allocate the channel via shared_ptr to prevent Use-After-Free
  auto ch = std::make_shared<ChannelType>(io, items.size());

  for (auto* item : items) {
    boost::asio::co_spawn(
        io,
        // FIX 3: Capture `ch` and `async_func` by value (copying the shared_ptr
        // and the lambda)
        [item, ch, async_func]() -> boost::asio::awaitable<void> {
          try {
            auto res = co_await async_func(item);
            ch->try_send(boost::system::error_code{}, res);
          } catch (const std::exception& e) {
            // FIX 4: Prevent deadlocks if the async_func throws an unexpected
            // exception
            spdlog::error("Parallel execution exception: {}", e.what());
            ch->try_send(boost::system::error_code{},
                         std::unexpected(hermes::config::ErrorInfo::From(
                             hermes::config::AppError::Critical, e.what())));
          }
        },
        boost::asio::detached);
  }

  std::expected<void, hermes::config::ErrorInfo> final_res;
  for (size_t i = 0; i < items.size(); ++i) {
    boost::system::error_code ec;
    auto res = co_await ch->async_receive(
        boost::asio::redirect_error(boost::asio::use_awaitable, ec));

    if (ec) {
      if (final_res.has_value()) {
        final_res = std::unexpected(hermes::config::ErrorInfo::From(
            hermes::config::AppError::NetworkError, "Channel sync error"));
      }
    } else if (!res && final_res.has_value()) {
      // Record the first error encountered, but keep waiting for all tasks to
      // finish
      final_res = res;
    }
  }

  ch->close();
  co_return final_res;
}
}  // namespace

#include "core/graph/Node.hpp"
#include "spdlog/spdlog.h"
#include "unordered_set"
using namespace hermes::service;

namespace hermes::audio {

std::expected<std::unique_ptr<AudioExecutor>, config::ErrorInfo>
AudioExecutor::create(boost::asio::io_context& io, const Graph& graph,
                      config::S3Config s3_config) {
  if (graph.start_node == nullptr) {
    spdlog::error("[AudioExecutor] Invalid graph: missing start node.");
    return std::unexpected(config::ErrorInfo::From(
        config::AppError::LogicError, "Invalid graph: missing start node."));
  }

  return std::make_unique<AudioExecutor>(io, graph, std::move(s3_config));
}

AudioExecutor::AudioExecutor(boost::asio::io_context& io, const Graph& graph,
                             config::S3Config s3_config)
    : io_(io), graph_(graph), s3_config_(std::move(s3_config)) {
  current_node_ = graph_.start_node;
}

SessionStats& AudioExecutor::get_stats() { return stats_; }

void AudioExecutor::detect_and_flag_loops() const {
  Node* curr = graph_.start_node;
  std::unordered_set<Node*> visited;
  Node* cycle_start = nullptr;

  while (curr != nullptr) {
    if (!visited.insert(curr).second) {
      cycle_start = curr;  // Found the start of the cycle
      break;
    }
    curr = curr->next();
  }

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

  auto fetch_res = co_await ensure_assets_exist();
  if (!fetch_res) {
    co_return std::unexpected(fetch_res.error());
  }

  auto init_res = co_await initialize_nodes();
  if (!init_res) {
    co_return std::unexpected(init_res.error());
  }

  update_mixers();

  detect_and_flag_loops();

  current_node_ = graph_.start_node;
  stats_.current_node_id = current_node_->id();
  stats_.total_bytes_sent = 0;

  co_return std::expected<void, config::ErrorInfo>();
}

boost::asio::awaitable<std::expected<void, config::ErrorInfo>>
AudioExecutor::ensure_assets_exist() {
  spdlog::info("Ensuring all file assets exist...");

  co_return co_await await_all_dynamic(
      io_, graph_.file_nodes,
      [this](auto* node) { return node->ensure_file_exists(s3_config_); });
}

boost::asio::awaitable<std::expected<void, config::ErrorInfo>>
AudioExecutor::initialize_nodes() {
  spdlog::info("Initializing async nodes...");

  for (const auto& node : graph_.nodes) {
    spdlog::debug("Initializing buffers for node [{}]", node->id());
    co_await node->initialize_buffers();
  }
  co_return std::expected<void, config::ErrorInfo>();
}

void AudioExecutor::update_mixers() {
  for (auto* mixer : graph_.mixer_nodes) {
    mixer->set_max_frames();
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

  auto result = current_node_->process_frame(output_buffer);

  if (!result && result.error().code == config::NodeErrorCode::NotSupported) {
    // Current node is not an audio processor, stop.
    return {false, config::NodeError{config::NodeErrorCode::Success, "", ""}};
  }

  if (!result && result.error().code != config::NodeErrorCode::EndOfStream) {
    // Underruns are non-critical, we tell the caller to continue (true)
    bool is_recoverable =
        (result.error().code == config::NodeErrorCode::Underrun);
    return {is_recoverable, result.error()};
  }

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
  auto close_result = current_node_->close();
  if (!close_result) {
    return std::unexpected(close_result.error());
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
