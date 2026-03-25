#include "ActiveSessions.hpp"

#include <spdlog/spdlog.h>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <memory>
#include <stdexcept>

#include "Config.hpp"
#include "Json2Graph.hpp"
#include "Nodes.hpp"
#include "WebSocketSessionObserver.hpp"

using namespace hermes::audio;
using namespace hermes::infra;
namespace hermes::service {
ActiveSessions::ActiveSessions(IoContextPool& pool,
                               const config::AppConfig& cfg)
    : pool_(pool), cfg_(cfg) {
  // filling the ports that janus can use for rtp streams
  for (uint16_t p = cfg_.janus.port_start; p <= cfg_.janus.port_end; ++p) {
    available_webrtc_ports_.push(p);
  }
}

std::expected<std::string, ErrorInfo> ActiveSessions::create_session(
    const boost::json::object& jobj, SessionType session_type) {
  std::string session_id = boost::uuids::to_string(generator_());  //
  next_session_id_.fetch_add(1, std::memory_order_relaxed);        //

  boost::asio::io_context& io = pool_.get_io_context();  //

  // Step 1: Parse the Graph
  auto graph_result =
      parse_graph(io, jobj).transform_error([&](const auto& err) {
        spdlog::error("Graph parsing failed for {}: {}", session_id,
                      err.message);
        return err;
      });  //

  if (!graph_result) return std::unexpected(graph_result.error());

  // Step 2: Handle WebRTC Resource Allocation
  std::optional<uint16_t> allocated_port = std::nullopt;
  if (session_type == SessionType::WebRTC) {
    allocated_port = allocate_webrtc_port();
    if (!allocated_port) {
      return std::unexpected(
          ErrorInfo::From(AppError::LogicError, "No available WebRTC ports"));
    }
  }

  // Step 3: Instantiate and Register
  auto session =
      std::make_shared<Session>(io, session_id, std::move(*graph_result),
                                cfg_.s3, (session_type == SessionType::WebRTC),
                                cfg_.janus.address, allocated_port);  //

  {
    std::lock_guard<std::mutex> lock(mutex_);  //
    sessions_[session_id] = std::move(session);
  }

  return session_id;
}

std::expected<void, ErrorInfo> ActiveSessions::create_and_run_websocket_session(
    const std::string& audio_session_id, const req_t& req,
    boost::beast::tcp_stream& stream) {  // 1. Validate Session Existence
  std::shared_ptr<Session> session =
      get(audio_session_id);  // Using existing get()
  if (!session) {
    return std::unexpected(
        ErrorInfo::From(AppError::LogicError, "Session not found"));
  }

  if (session->is_running()) {
    return std::unexpected(
        ErrorInfo::From(AppError::LogicError, "WebSocket already connected"));
  }

  auto websocket =
      std::make_shared<WebSocketSession>(stream.release_socket());  //
  websocket->do_accept(req);                                        //

  session->attach_observer(
      std::make_unique<WebSocketSessionObserver>(websocket));  //

  {
    std::lock_guard<std::mutex> lock(mutex_);
    websocket_sessions_[audio_session_id] = websocket;
  }

  spawn_session_coroutine(audio_session_id, session, websocket);

  return {};
}

std::shared_ptr<Session> ActiveSessions::get(const std::string& id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = sessions_.find(id);
  if (it == sessions_.end()) {
    return nullptr;
  }
  return it->second;
}

void ActiveSessions::spawn_session_coroutine(
    const std::string& id, std::shared_ptr<Session> session,
    std::shared_ptr<net::websocket::WebSocketSession> ws) {
  // We spawn the coroutine on the session's specific io_context from the pool
  asio::co_spawn(
      pool_.get_io_context(),
      [this, self = shared_from_this(), id, sess = std::move(session),
       websocket = std::move(ws)]() -> asio::awaitable<void> {
        try {
          // The main audio loop runs here (20ms ticks)
          co_await sess->start();
        } catch (const std::exception& e) {
          spdlog::error("[{}] Unhandled session exception: {}", id, e.what());
        }

        // Guaranteed cleanup:
        // Reclaims WebRTC ports and erases the session/websocket from the maps.
        remove_session(id);
      },
      asio::detached);
}

std::optional<uint16_t> ActiveSessions::allocate_webrtc_port() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (available_webrtc_ports_.empty()) {
    return std::nullopt;
  }
  uint16_t port = available_webrtc_ports_.front();
  available_webrtc_ports_.pop();
  return port;
}

ActiveSessions::SessionOpStatus ActiveSessions::remove_session(
    const std::string& id) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto session_it = sessions_.find(id);
  if (session_it == sessions_.end()) {
    return SessionOpStatus::SessionNotFound;
  }

  auto port = session_it->second->get_webrtc_port();
  if (port.has_value()) {
    available_webrtc_ports_.push(*port);
    spdlog::debug("[{}] Reclaimed WebRTC port {}", id, *port);
  }

  session_it->second->stop();
  sessions_.erase(session_it);
  spdlog::info("[{}] Audio session stopped and removed.", id);

  auto ws_it = websocket_sessions_.find(id);
  if (ws_it != websocket_sessions_.end()) {
    ws_it->second->close();
    websocket_sessions_.erase(ws_it);
    spdlog::info("[{}] WebSocket session detached and closed.", id);

    return SessionOpStatus::Success;
  }

  // This is technically a success, but we report the detail.
  spdlog::warn("[{}] Audio removed, but WebSocket was missing.", id);
  return SessionOpStatus::WebSocketNotFound;
}

ActiveSessions::SessionOpStatus ActiveSessions::pause_session(
    const std::string& id) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto session_it = sessions_.find(id);
  if (session_it == sessions_.end()) {
    return SessionOpStatus::SessionNotFound;
  }

  session_it->second->pause();

  return SessionOpStatus::Success;
}

ActiveSessions::SessionOpStatus ActiveSessions::resume_session(
    const std::string& id) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto session_it = sessions_.find(id);
  if (session_it == sessions_.end()) {
    return SessionOpStatus::SessionNotFound;
  }

  session_it->second->resume();

  return SessionOpStatus::Success;
}

std::size_t ActiveSessions::size() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return sessions_.size();
}

std::vector<SessionRtpStats> ActiveSessions::get_all_session_rtp_stats() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<SessionRtpStats> stats;
  stats.reserve(sessions_.size());
  for (const auto& [id, session] : sessions_) {
    stats.push_back(
        {id, session->get_rtp_bytes_sent(), session->get_rtp_packets_sent()});
  }
  return stats;
}

uint64_t ActiveSessions::get_total_sessions_created() const {
  return next_session_id_.load(std::memory_order_relaxed);
}

std::size_t ActiveSessions::get_active_websockets_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return websocket_sessions_.size();
}

std::size_t ActiveSessions::get_available_webrtc_ports_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return available_webrtc_ports_.size();
}

}  // namespace hermes::service
