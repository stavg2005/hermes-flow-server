#pragma once

#include "Router.hpp"
#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/beast.hpp>
#include <memory>
#include "types.hpp"


namespace server::core {

class HttpSession : public std::enable_shared_from_this<HttpSession> {
  beast::tcp_stream stream_;
  std::shared_ptr<Router> router_;
  beast::flat_buffer buffer_;

  // We keep parser_ as a member, but it will be reset inside the loop
  std::shared_ptr<http::request_parser<http::string_body>> parser_;

public:
  HttpSession(tcp::socket &&socket, const std::shared_ptr<Router> &router);

  // This is the public entry point, it will LAUNCH the coroutine
  void run();

private:
  // --- This is the new coroutine that contains ALL logic ---
  asio::awaitable<void> do_session();

  // --- This helper also becomes an awaitable coroutine ---
  asio::awaitable<void> do_graceful_close();

  asio::awaitable<std::tuple<beast::error_code, bool>> do_read_request();
  asio::awaitable<beast::error_code>
  do_write_response(http::response<http::string_body> &res);

  http::response<http::string_body> do_build_response();
  // --- These synchronous helpers remain ---
  bool is_options_request() const;
  void reset_for_next_request();
  void do_close();
};

// The fail function is fine as a free function
void fail(beast::error_code ec, const char *what);

} // namespace server::core
