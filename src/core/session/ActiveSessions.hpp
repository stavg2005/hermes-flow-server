#pragma once

#include <atomic>
#include <boost/asio/io_context.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http.hpp>
#include <boost/json.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

#include "IoContextPool.hpp"
#include "Session.hpp"
#include "WebSocketSession.hpp"

using namespace hermes::infra;
namespace hermes::service {

struct SessionRtpStats {
  std::string id;
  uint64_t bytes_sent;
  uint64_t packets_sent;
};

/**
 * @brief manages session lifecycle and websocket association.
 */
class ActiveSessions : public std::enable_shared_from_this<ActiveSessions> {
 public:
  using req_t = http::request<http::string_body>;

  explicit ActiveSessions(IoContextPool& pool, const config::AppConfig& cfg);

  /**
   * @brief Factory method to spawn a new Audio Session.
   * Expects a JSON object containing the Flow Graph definition.
   * Structure:
   * {
   *   "flow": {
   *     "nodes": [ { "id": "...", "type": "...", "data": {...} }, ... ],
   *     "edges": [ { "source": "...", "target": "..." }, ... ],
   *     "start_node": { "id": "..." }
   *   }
   * }
   *
   * @return The unique session ID (string) to be returned to the client.
   */
  std::expected<std::string, ErrorInfo> create_session(
      const boost::json::object& jobj, SessionType session_type);

  /**
   * @brief Upgrades connection to WebSocket and attaches observer to the audio
   * session.
   * @param stream The underlying TCP stream (moved from the HTTP handler).
   */
  std::expected<void, ErrorInfo> create_and_run_websocket_session(
      const std::string& audio_session_id, const req_t& req,
      boost::beast::tcp_stream& stream);

  /**
   * @brief Internal helper to allocate a WebRTC port from the pool.
   * @return The allocated port or std::nullopt if pool is empty.
   */
  std::optional<uint16_t> allocate_webrtc_port();

  /**
   * @brief Encapsulates the logic for attaching a WebSocket and spawning the session.
   */
  void spawn_session_coroutine(const std::string& id,
                               std::shared_ptr<Session> session,
                               std::shared_ptr<net::websocket::WebSocketSession> ws);

  enum class SessionOpStatus { Success, SessionNotFound, WebSocketNotFound };  // NOLINT
  SessionOpStatus remove_session(const std::string& id);

  SessionOpStatus pause_session(const std::string& id);

  ActiveSessions::SessionOpStatus resume_session(const std::string& id);

  std::shared_ptr<Session> get(const std::string& id) const;
  std::vector<std::string> list_ids() const;
  std::size_t size() const noexcept;

  std::vector<SessionRtpStats> get_all_session_rtp_stats() const;
  uint64_t get_total_sessions_created() const;
  std::size_t get_active_websockets_count() const;
  std::size_t get_available_webrtc_ports_count() const;

  void stop_all();
  void on_session_stopped(const std::string& id);

  ActiveSessions(const ActiveSessions&) = delete;
  ActiveSessions& operator=(const ActiveSessions&) = delete;

 private:
  mutable std::mutex mutex_;

  IoContextPool& pool_;
  /// Monotonic counter for observability only — NOT the session key (UUIDs are used for that).
  std::atomic<int64_t> next_session_id_{0};
  boost::uuids::random_generator generator_;
  config::AppConfig cfg_;
  std::queue<uint16_t> available_webrtc_ports_;
  std::unordered_map<std::string, std::shared_ptr<Session>> sessions_;
  std::unordered_map<std::string,
                     std::shared_ptr<net::websocket::WebSocketSession>>
      websocket_sessions_;
};
}  // namespace hermes::service
