#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/beast.hpp>
#include <boost/url/url_view.hpp>
#include <expected>
#include <memory>

#include "ActiveSessions.hpp"
#include "IoContextPool.hpp"
#include "Types.hpp"
#include "WebSocketSessionObserver.hpp"
#include "response_builder.hpp"

namespace hermes::net::http {

using req_t = boost::beast::http::request<boost::beast::http::string_body>;
using res_t = boost::beast::http::response<boost::beast::http::string_body>;

/**
 * @struct RouteError
 * @brief Simple structure to hold routing errors without relying on C++
 * exceptions.
 * * Allows the router to gracefully fail and bubble up standard HTTP status
 * codes and messages to be formatted by the response builder.
 */
struct RouteError {
  /// The HTTP status code representing the error (e.g., 400 Bad Request, 404
  /// Not Found).
  boost::beast::http::status code;
  /// A human-readable description of the error for the client response body.
  std::string message;
};

/**
 * @class Router
 * @brief Maps incoming HTTP requests to specific application endpoints and
 * handlers.
 * * Acts as the control plane interface for Hermes-Flow. It parses the request
 * target, delegates the payload to the appropriate session creation/management
 * functions, and handles WebSocket connection upgrades.
 */
class Router {
 public:
  /**
   * @brief Constructs the Router with dependencies injected.
   * * @param active Reference to the ActiveSessions manager to create and
   * control audio sessions.
   * @param pool Shared pointer to the I/O context pool for spawning
   * asynchronous tasks.
   */
  explicit Router(hermes::service::ActiveSessions& active,
                  std::shared_ptr<infra::IoContextPool> pool);

  /**
   * @brief Main request dispatcher.
   * * Parses the URI target of the HTTP request and routes it to the correct
   * private handler method. Modifies the `res` object in place with the result.
   * * @param req The incoming HTTP request.
   * @param res The outgoing HTTP response to be populated.
   * @param stream The underlying TCP stream, passed in case an upgrade to
   * WebSocket is required.
   */
  void route_query(const req_t& req, res_t& res,
                   boost::beast::tcp_stream& stream);

 private:
  // =========================================================================
  // Endpoint Handlers
  // =========================================================================

  /**
   * @brief Handles POST requests to create a standard, unencrypted RTP audio
   * session.
   * @return std::expected<void, RouteError> Void on success, or routing error.
   */
  std::expected<void, RouteError> handle_transmit(const req_t& req, res_t& res);

  /**
   * @brief Handles POST requests to create an encrypted (AES-CTR SRTP) audio
   * session.
   * @return std::expected<void, RouteError> Void on success, or routing error.
   */
  std::expected<void, RouteError> handle_secure_transmit(const req_t& req,
                                                         res_t& res);

  /**
   * @brief Handles requests to create a WebRTC-compatible audio session.
   * Allocates Janus gateway ports and configures the graph for WebRTC
   * endpoints.
   * @return std::expected<void, RouteError> Void on success, or routing error.
   */
  std::expected<void, RouteError> handle_webrtc_request(const req_t& req,
                                                        res_t& res);

  /**
   * @brief Handles HTTP GET requests asking to upgrade to a WebSocket
   * connection.
   * * Expects a valid session ID query parameter. If successful, socket
   * ownership is transferred to a new `WebSocketSession`.
   * * @param stream The TCP stream to be upgraded and moved.
   * @return std::expected<void, RouteError> Void on success, or routing error.
   */
  std::expected<void, RouteError> handle_websocket_request(
      const req_t& req, res_t& res, boost::beast::tcp_stream& stream);

  // =========================================================================
  // Session Lifecycle Control Handlers
  // =========================================================================

  /**
   * @brief Handles DELETE requests to cleanly stop and teardown an active
   * session.
   */
  std::expected<void, RouteError> handle_stop(const req_t& req, res_t& res);

  /**
   * @brief Handles POST requests to temporarily pause the audio processing
   * graph.
   */
  std::expected<void, RouteError> handle_pause(const req_t& req, res_t& res);

  /**
   * @brief Handles POST requests to resume a paused audio processing graph.
   */
  std::expected<void, RouteError> handle_resume(const req_t& req, res_t& res);

  /**
   * @brief Handles GET requests to retrieve system metrics in Prometheus
   * format.
   */
  std::expected<void, RouteError> handle_metrics(const req_t& req, res_t& res);

  // =========================================================================
  // Internal Helpers
  // =========================================================================

  /**
   * @brief Shared helper for session creation endpoints.
   * Validates the request body, triggers the session factory, and formats the
   * response.
   * * @param session_type The specific type of session to spawn (Standard,
   * Encrypted, WebRTC).
   * @param endpoint_name Used for logging context.
   */
  std::expected<void, RouteError> process_session_request(
      const req_t& req, res_t& res, config::SessionType session_type,
      std::string_view endpoint_name);

  /// A callable that takes a session ID string and performs an operation,
  /// returning its status.
  using SessionOp = std::function<service::ActiveSessions::SessionOpStatus(
      const std::string&)>;

  /**
   * @brief Shared helper for lifecycle endpoints (Stop, Pause, Resume).
   * Extracts the `?id=` parameter from the URL, executes the provided
   * operation, and maps the operation's result status to the appropriate HTTP
   * response.
   */
  std::expected<void, RouteError> handle_session_id_op(const req_t& req,
                                                       res_t& res,
                                                       SessionOp op);

  // =========================================================================
  // Member Variables
  // =========================================================================

  /// Reference to the centralized session manager. Server owns both, and Server
  /// ensures Router dies before ActiveSessions.
  hermes::service::ActiveSessions& active_;

  /// Thread pool used for dispatching detached tasks or background operations.
  std::shared_ptr<infra::IoContextPool> pool_;

  /// Counter tracking the total number of HTTP requests routed (used for
  /// metrics).
  std::atomic<uint64_t> http_requests_total_{0};
};

}  // namespace hermes::net::http
