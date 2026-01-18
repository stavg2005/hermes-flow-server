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
#include <string>
#include <unordered_map>
#include <vector>

#include "IoContextPool.hpp"
#include "Session.hpp"
#include "WebSocketSession.hpp"
using namespace hermes::infra;
namespace hermes::service {
/**
 * @brief manages session lifecycle and websocket association.
 */
class ActiveSessions : public std::enable_shared_from_this<ActiveSessions> {
 public:
  using req_t = http::request<http::string_body>;

  explicit ActiveSessions(std::shared_ptr<IoContextPool> pool);

  /**
   * @brief Factory method to spawn a new Audio Session.
   * @return The unique session ID (string) to be returned to the client.
   */
  std::expected<std::string, ErrorInfo> create_session(
      const boost::json::object& jobj);

  /**
   * @brief Upgrades connection to WebSocket and attaches observer to the audio
   * session.
   * @param stream The underlying TCP stream (moved from the HTTP handler).
   */
  std::expected<void, ErrorInfo> create_and_run_WebsocketSession(
      const std::string& audio_session_id, const req_t& req,
      boost::beast::tcp_stream& stream);

  enum class RemoveStatus { Success, SessionNotFound, WebSocketNotFound };
  RemoveStatus remove_session(const std::string& id);


  std::shared_ptr<Session> get(const std::string& id) const;
  std::vector<std::string> list_ids() const;
  std::size_t size() const noexcept;


  void stop_all();
  void on_session_stopped(const std::string& id);


  ActiveSessions(const ActiveSessions&) = delete;
  ActiveSessions& operator=(const ActiveSessions&) = delete;

 private:
  mutable std::mutex mutex_;

  std::shared_ptr<IoContextPool> pool_;
  std::atomic<int64_t> next_session_id_{0};
  boost::uuids::random_generator generator_;

  
  std::unordered_map<std::string, std::shared_ptr<Session>> sessions_;
  std::unordered_map<std::string,
                     std::shared_ptr<net::websocket::WebSocketSession>>
      websocket_sessions_;
};
}  // namespace hermes::service
