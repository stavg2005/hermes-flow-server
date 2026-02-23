#include "Session.hpp"

#include <spdlog/spdlog.h>

#include <array>
#include <boost/asio.hpp>
#include <boost/asio/experimental/channel.hpp>
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

Session::Session(asio::io_context& io, std::string id, Graph&& g,
                 config::S3Config& s3_config, bool is_web_rtc,
                 std::string janus_ip, std::optional<uint16_t> janus_port)
    : io_(io),
      id_(std::move(id)),
      resume_channel_(io_, 1),
      timer_(io),
      graph_(std::make_unique<Graph>(std::move(g))),
      janus_ip_(std::move(janus_ip)),
      janus_port_(janus_port) {
  audio_executor_ = std::make_unique<AudioExecutor>(io_, *graph_, s3_config);
  streamer_ = std::make_unique<RTPStreamer>(io_);
  is_webrtc_ = true;
  spdlog::debug("Session [{}] created.", id_);
}

void Session::attach_observer(std::unique_ptr<ISessionObserver> observer) {
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

void Session::pause() {
  if (!is_paused_) {
    is_paused_ = true;
  }
}

void Session::resume() {
  if (is_paused_) {
    is_paused_ = false;

    // Drop a dummy signal into the channel.
    // try_send is synchronous, so it can be called from standard functions.
    resume_channel_.try_send(boost::system::error_code{});
  }
}
// TODO save the clients node in a data memeber in graph when parsing the graph
void Session::configure_streamer_from_graph() {
  if (is_webrtc_) {
    if (janus_port_.has_value()) {
      spdlog::info("[{}] Registering Janus WebRTC target: {}:{}", id_,
                   janus_ip_, *janus_port_);
      streamer_->add_client(janus_ip_, *janus_port_);
      observer_->on_webrtc_request(*janus_port_);

    } else {
      spdlog::error("[{}] WebRTC Session started without an allocated port!",
                    id_);
    }
  } else {
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
}

asio::awaitable<void> Session::start() {
  spdlog::info("[{}] Starting session execution...", id_);
  is_running_ = true;

  NodeError exit_reason;
  exit_reason.code = NodeErrorCode::Success;

  try {
    if (!co_await initialize_graph_execution()) {
      exit_reason.code = NodeErrorCode::InitializationFailed;
      // Do not co_return here. Let the flow drop to the cleanup section.
    } else {
      configure_streamer_from_graph();
      auto next_tick = std::chrono::steady_clock::now();
      auto last_stats_time = next_tick;
      std::array<uint8_t, FRAME_SIZE_BYTES> pcm_buffer;

      while (is_running_) {
        if (is_paused_) {
          spdlog::info("[{}] Paused. Going to sleep...", id_);

          boost::system::error_code ec;
          co_await resume_channel_.async_receive(
              asio::redirect_error(asio::use_awaitable, ec));

          spdlog::info("[{}] Woke up!", id_);

          next_tick = std::chrono::steady_clock::now();
          last_stats_time = next_tick;
          continue;
        }

        next_tick += std::chrono::milliseconds(20);

        timer_.expires_at(next_tick);

        boost::system::error_code timer_ec;
        co_await timer_.async_wait(
            asio::redirect_error(asio::use_awaitable, timer_ec));

        if (timer_ec == boost::asio::error::operation_aborted) {
          spdlog::debug("[{}] Timer cancelled, stopping loop.", id_);
          break;
        }
        // Ignore other timer_ec errors (like timeouts if it's already in the
        // past)

        auto [executor_wants_continue, status] =
            audio_executor_->get_next_frame(pcm_buffer);

        if (!is_status_ok(status.code)) {
          exit_reason.code = status.code;
          break;
        }

        if (!executor_wants_continue) {
          spdlog::info("[{}] Executor reached natural finish.", id_);
          break;
        } 

        streamer_->send_frame(pcm_buffer);
        audio_executor_->get_stats().packets_sent++;
        update_stats_if_needed(last_stats_time);
      }
    }
  } catch (const std::exception& e) {
    spdlog::error("[{}] Unhandled exception in session loop: {}", id_,
                  e.what());
    exit_reason.code =
        NodeErrorCode::InternalError;  // Or appropriate generic error
  }

  // Guaranteed Teardown
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
    audio_executor_->get_stats().underruns++;
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
