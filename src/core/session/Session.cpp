#include "Session.hpp"

#include <spdlog/spdlog.h>

#include <array>
#include <boost/asio.hpp>
#include <chrono>
#include <memory>
#include <string>

#include "AudioExecutor.hpp"
#include "ISessionObserver.hpp"
#include "Nodes.hpp"
#include "RTPStreamer.hpp"
#include "Types.hpp"
using namespace hermes::audio;
using namespace hermes::config;
namespace hermes::service {
Session::Session(asio::io_context& io, std::string id, Graph&& g)
    : io_(io),
      id_(std::move(id)),
      timer_(io),
      graph_(std::make_shared<Graph>(std::move(g))) {
  audio_executor_ = std::make_unique<AudioExecutor>(io_, graph_);
  streamer_ = std::make_unique<RTPStreamer>(io_);

  spdlog::debug("Session [{}] created.", id_);
}

void Session::attach_observer(std::shared_ptr<ISessionObserver> observer) {
  observer_ = std::move(observer);
  spdlog::info("[{}] Observer attached.", id_);
}

void Session::add_client(const std::string& ip, uint16_t port) {
  streamer_->add_client(ip, port);
}

void Session::stop() {
  spdlog::info("[{}] Stopping session...", id_);
  is_running_ = false;

  timer_.cancel();
}

void Session::configure_streamer_from_graph() {
  for (const auto& node : graph_->nodes) {
    if (node->kind() == NodeKind::Clients) {
      auto* clientsNode = static_cast<ClientsNode*>(node.get());

      for (const auto& [ip, port] : clientsNode->clients) {
        spdlog::info("[{}] Auto-registering client: {}:{}", id_, ip, port);
        streamer_->add_client(ip, port);
      }
    }
  }
}
asio::awaitable<void> Session::start() {
  spdlog::info("[{}] Starting session execution...", id_);
  is_running_ = true;

  if (!co_await initialize_graph_execution()) {
    // InitializeGraphExecution already handled the logging/observer for the
    // error
    stop();
    co_return;
  }

  configure_streamer_from_graph();

  auto next_tick = std::chrono::steady_clock::now();
  auto last_stats_time = std::chrono::steady_clock::now();
  std::array<uint8_t, FRAME_SIZE_BYTES> pcm_buffer;

  // Track why the session ended. Default is Success (natural end).
  NodeError exit_reason;
  exit_reason.code = NodeErrorCode::Success;

  while (is_running_) {
    next_tick += std::chrono::milliseconds(20);
    timer_.expires_at(next_tick);
    try {
      // If timer is cancelled, this will throw
      co_await timer_.async_wait(asio::use_awaitable);
    } catch (const boost::system::system_error& e) {
      if (e.code() == boost::asio::error::operation_aborted) {
        spdlog::debug("[{}] Timer cancelled, stopping loop.", id_);
        break;  // Exit the loop gracefully
      }
      throw;  // Rethrow real errors
    }

    auto [executor_wants_continue, status] =
        audio_executor_->get_next_frame(pcm_buffer);

    if (!is_status_ok(status.code)) {
      exit_reason.code = status.code;
      break;  // Stop immediately on critical error
    }

    if (!executor_wants_continue) {
      // Natural finish (exit_reason remains Success)
      break;
    }

    streamer_->send_frame(pcm_buffer);
    update_stats_if_needed(last_stats_time);
  }

  is_running_ = false;

  finalize_session(exit_reason);
}

asio::awaitable<bool> Session::initialize_graph_execution() {
  auto prepare_result = co_await audio_executor_->prepare();

  if (!prepare_result) {
    auto err = prepare_result.error();
    spdlog::error("[{}] Audio preparation failed: {}", id_, err.message);
    if (observer_) {
      observer_->on_error("Prep Failed: " + err.message);
    }
    co_return false;
  }
  co_return true;
}

bool Session::is_status_ok(NodeErrorCode code) {
  if (code == NodeErrorCode::Success) return true;

  // Warnings / Info (Non-breaking)
  if (code == NodeErrorCode::Underrun) {
    spdlog::warn("[{}] Underrun detected (inserting silence)", id_);
    return true;
  }
  if (code == NodeErrorCode::EndOfStream) {
    // Just debug log; the 'executor_wants_continue' bool in start()
    // handles the actual stopping logic for EOS.
    spdlog::debug("[{}] Node reached EndOfStream", id_);
    return true;
  }

  // Critical Error
  spdlog::error("[{}] Critical error during frame processing: {}", id_,
                to_string(code));
  return false;
}

void Session::finalize_session(const NodeError& result) {
  if (!observer_) return;

  if (result.code != NodeErrorCode::Success) {
    spdlog::error("[{}] Session ended with error: {}", id_, result.message);
    observer_->on_error(result.message);
  } else {
    spdlog::info("[{}] Session finished successfully.", id_);
    observer_->on_session_complete();
  }
}

void Session::update_stats_if_needed(
    std::chrono::steady_clock::time_point& last_stats_time) {
  if (!observer_) return;

  auto now = std::chrono::steady_clock::now();
  if (now - last_stats_time > std::chrono::milliseconds(100)) {
    observer_->on_stats_update(audio_executor_->get_stats());
    last_stats_time = now;
  }
}

bool Session::is_running() const { return is_running_; }

Session::~Session() = default;
}  // namespace hermes::service
