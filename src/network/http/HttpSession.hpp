#pragma once

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/beast.hpp>
#include <expected>
#include <memory>

#include "Router.hpp"
#include "Types.hpp"

namespace hermes::net::http {

/**
 * @class HttpSession
 * @brief Handles a single HTTP connection.
 * * Manages the lifecycle of the socket until it is either upgraded to a
 * WebSocket connection or safely closed. Utilizes Boost.Asio coroutines
 * for asynchronous request processing.
 */
class HttpSession : public std::enable_shared_from_this<HttpSession> {
 public:
  /**
   * @brief Factory method to securely instantiate an HttpSession.
   * * @param socket The underlying TCP socket for the connection.
   * @param router Shared pointer to the router handling request dispatching.
   * @return std::expected<std::shared_ptr<HttpSession>, config::ErrorInfo>
   * A shared pointer to the created session on success, or an ErrorInfo on
   * failure.
   */
  static std::expected<std::shared_ptr<HttpSession>, config::ErrorInfo> create(
      boost::asio::ip::tcp::socket socket, std::shared_ptr<Router> router);

  /**
   * @brief Starts the asynchronous execution of the HTTP session.
   * * Spawns the internal `do_session` coroutine to begin reading and
   * processing incoming HTTP requests.
   */
  void run();

 private:
  /**
   * @brief Private constructor to enforce the use of the `create` factory
   * method.
   * * @param socket The underlying TCP socket (moved).
   * @param router Shared pointer to the application router.
   */
  HttpSession(boost::asio::ip::tcp::socket&& socket,
              std::shared_ptr<Router> router);

  // =========================================================================
  // Core Logic
  // =========================================================================

  /**
   * @brief Main asynchronous loop for the HTTP session.
   * * Continuously reads and processes requests until the connection is closed,
   * an error occurs, or the connection is upgraded to a WebSocket.
   * * @return boost::asio::awaitable<void>
   */
  boost::asio::awaitable<void> do_session();

  /**
   * @brief Handles malformed or invalid HTTP requests.
   * * Builds and sends a 400 Bad Request HTTP response to the client.
   * * @param error_msg The specific error message to include in the response
   * body.
   * @return boost::asio::awaitable<void>
   */
  boost::asio::awaitable<void> handle_bad_request(const std::string& error_msg);

  /**
   * @brief Checks for and executes a WebSocket upgrade if requested.
   * * @param req The incoming HTTP request to inspect for upgrade headers.
   * @return true If the connection was successfully upgraded to a WebSocket.
   * @return false If the request is a standard HTTP request (no upgrade).
   */
  bool handle_websocket_upgrade(
      const boost::beast::http::request<boost::beast::http::string_body>& req);

  /**
   * @brief Processes a single HTTP request from start to finish.
   * * @param keep_alive Indicates whether the connection should be kept alive
   * after the response.
   * @return boost::asio::awaitable<bool> Returns true if the session should
   * continue processing further requests.
   */
  boost::asio::awaitable<bool> process_single_request(bool keep_alive);

  // =========================================================================
  // I/O Operations
  // =========================================================================

  /**
   * @brief Asynchronously reads an HTTP request from the stream.
   * * @return boost::asio::awaitable<std::expected<bool, config::ErrorInfo>>
   * Returns true on successful read, false on graceful EOF, or an error.
   */
  boost::asio::awaitable<std::expected<bool, config::ErrorInfo>>
  do_read_request();

  /**
   * @brief Asynchronously writes an HTTP response to the stream.
   * * @param res The HTTP response object to be sent to the client.
   * @return boost::asio::awaitable<std::expected<void, config::ErrorInfo>>
   * Void on success, or an error if the write fails.
   */
  boost::asio::awaitable<std::expected<void, config::ErrorInfo>>
  do_write_response(
      boost::beast::http::response<boost::beast::http::string_body>& res);

  /**
   * @brief Gracefully shuts down the TCP stream connection.
   * * @return boost::asio::awaitable<std::expected<void, config::ErrorInfo>>
   */
  boost::asio::awaitable<std::expected<void, config::ErrorInfo>>
  do_graceful_close();

  // =========================================================================
  // Helpers
  // =========================================================================

  /**
   * @brief Determines if the current parsed request is an HTTP OPTIONS request.
   * * Commonly used for handling CORS preflight checks.
   * * @return true If the request method is OPTIONS.
   * @return false Otherwise.
   */
  bool is_options_request() const;

  /**
   * @brief Routes the parsed request and builds the appropriate HTTP response.
   * * @return boost::beast::http::response<boost::beast::http::string_body>
   * The constructed HTTP response ready to be written to the socket.
   */
  boost::beast::http::response<boost::beast::http::string_body>
  do_build_response();

  // =========================================================================
  // Member Variables
  // =========================================================================

  /// The Boost.Beast TCP stream wrapper providing timeout and async I/O safety.
  boost::beast::tcp_stream stream_;

  /// Pointer to the router responsible for mapping requests to their handlers.
  std::shared_ptr<Router> router_;

  /// The memory buffer used for reading incoming HTTP request data.
  boost::beast::flat_buffer buffer_;

  /// Optional parser used to incrementally parse the HTTP request.
  /// Recreated for each new request to ensure a clean state.
  std::optional<
      boost::beast::http::request_parser<boost::beast::http::string_body>>
      parser_;
};

}  // namespace hermes::net::http
