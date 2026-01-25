#pragma once

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/beast.hpp>
#include <memory>

#include "Router.hpp"
#include "Types.hpp"

namespace hermes::net::http {

/**
 * @brief Handles a single HTTP connection.
 * Manages the lifecycle of the socket until it is upgraded (WebSocket) or
 * closed.
 */
class HttpSession : public std::enable_shared_from_this<HttpSession> {
 public:
  static std::expected<std::shared_ptr<HttpSession>, ErrorInfo> create(
      tcp::socket socket, std::shared_ptr<Router> router);

  // Entry point: launches the session coroutine
  void run();

 private:
  HttpSession(tcp::socket&& socket, std::shared_ptr<Router> router);
  // Core Logic
  asio::awaitable<void> do_session();


  asio::awaitable<std::expected<bool, ErrorInfo>> do_read_request();


  asio::awaitable<std::expected<void, ErrorInfo>> do_write_response(
      beast::http::response<beast::http::string_body>& res);


  asio::awaitable<std::expected<void, ErrorInfo>> do_graceful_close();

  bool is_options_request() const;
  beast::http::response<beast::http::string_body> do_build_response();
  beast::tcp_stream stream_;
  std::shared_ptr<Router> router_;
  beast::flat_buffer buffer_;

  // Use optional to reuse parser memory
  std::optional<beast::http::request_parser<beast::http::string_body>> parser_;
};

}  // namespace hermes::net::http
