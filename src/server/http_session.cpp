#include "http_session.hpp"

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
#include <chrono>
#include <memory>

#include "Router.hpp"
#include "spdlog/spdlog.h"

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
static const int SESSION_TIMEOUT_SECONDS = 30;
static const int DRAIN_BUFFER_SIZE = 1024;
using tcp = net::ip::tcp;

namespace server::core {

// fail() function is unchanged...
void fail(beast::error_code ec, const char *what) {
  if (ec != beast::errc::not_connected && ec != net::error::eof &&
      ec != net::error::connection_reset) {
    spdlog::error(what);
  }
}

// Constructor is unchanged...
HttpSession::HttpSession(tcp::socket &&socket,
                         const std::shared_ptr<Router> &router)
    : stream_(std::move(socket)), router_(router) {
  beast::error_code ec;
  auto remote = stream_.socket().remote_endpoint(ec);
  if (!ec) {
    spdlog::info("New connection from: {}:{}", remote.address().to_string(),
                 remote.port());
  }
}

void HttpSession::run() {
  net::co_spawn(
      stream_.get_executor(),
      [self = shared_from_this()]() { return self->do_session(); },
      net::detached);
}

net::awaitable<void> HttpSession::do_session() {
  for (;;) {
    // Reset state
    parser_ = std::make_shared<http::request_parser<http::string_body>>();
    stream_.expires_after(std::chrono::seconds(SESSION_TIMEOUT_SECONDS));

    // Read the request
    auto [ec_read, keep_alive] = co_await do_read_request();
    if (ec_read) {
      if (ec_read == http::error::end_of_stream) {
        co_await do_graceful_close();
      } else {
        fail(ec_read, "read");
      }
      co_return;  // Stop session
    }

    // Process the request and build a response
    http::response<http::string_body> res = do_build_response();

    // write the response
    auto ec_write = co_await do_write_response(res);
    if (ec_write) {
      fail(ec_write, "write");
      co_return;  // Stop session
    }

    // 5. Handle keep-alive
    if (!keep_alive) {
      spdlog::info("closing http session");
      co_await do_graceful_close();
      co_return;  // Stop session
    }

    // Reset for the next loop
    reset_for_next_request();
  }
}

net::awaitable<std::tuple<beast::error_code, bool>>
HttpSession::do_read_request() {
  // Read Header
  auto [ec_header, bytes_header] = co_await http::async_read_header(
      stream_, buffer_, *parser_, net::as_tuple(net::use_awaitable));

  if (ec_header) {
    co_return std::make_tuple(ec_header, false);
  }

  // Read Body (if not OPTIONS)
  if (!is_options_request()) {
    stream_.expires_after(std::chrono::seconds(SESSION_TIMEOUT_SECONDS));

    auto [ec_body, bytes_body] = co_await http::async_read(
        stream_, buffer_, *parser_, net::as_tuple(net::use_awaitable));
    if (ec_body) {
      co_return std::make_tuple(ec_body, false);
    }
  }

  // Success
  co_return std::make_tuple(beast::error_code{}, parser_->get().keep_alive());
}

http::response<http::string_body> HttpSession::do_build_response() {
  http::response<http::string_body> res;
  const auto &req = parser_->get();

  if (is_options_request()) {
    server::models::ResponseBuilder::build_options_response(res, req.version(),
                                                            req.keep_alive());
  } else {
    res.version(req.version());
    res.keep_alive(req.keep_alive());
    spdlog::info("Routing");
    router_->RouteQuery(req, res);
  }
  return res;  // Return the response by value
}

net::awaitable<beast::error_code> HttpSession::do_write_response(
    http::response<http::string_body> &res) {
  beast::error_code ec_opt;
  ec_opt = stream_.socket().set_option(tcp::no_delay(true), ec_opt);
  if (ec_opt) {
    fail(ec_opt, "set_option (TCP_NODELAY)");
  }

  auto [ec_write, bytes_write] = co_await http::async_write(
      stream_, res, net::as_tuple(net::use_awaitable));

  if (ec_write) {
    co_return ec_write;
  }

  spdlog::info("Response sent: {}", bytes_write);
  co_return beast::error_code{};
}

net::awaitable<void> HttpSession::do_graceful_close() {
  spdlog::info("graceful closed");
  beast::error_code ec;
  ec = stream_.socket().shutdown(tcp::socket::shutdown_send, ec);
  if (ec && ec != beast::errc::not_connected) {
    fail(ec, "shutdown");
  }

  stream_.expires_after(std::chrono::seconds(SESSION_TIMEOUT_SECONDS));

  // Drain the socket to read the FIN packet
  beast::flat_buffer drain_buffer;

  // We'll use try/catch here, since we don't care about the result,
  // just that the operation finished.
  try {
    co_await stream_.async_read_some(
        drain_buffer.prepare(DRAIN_BUFFER_SIZE),
        net::use_awaitable  // No as_tuple, we'll catch exceptions
    );
  } catch (const std::exception &) {
    // Ignore all errors during drain (e.g., client hung up)
  }

  // The coroutine resumes here. Now we do the final close.
  do_close();
}

bool HttpSession::is_options_request() const {
  return parser_->get().method() == http::verb::options;
}

void HttpSession::reset_for_next_request() {
  buffer_.consume(buffer_.size());
  parser_.reset();
}

void HttpSession::do_close() {
  beast::error_code ec;
  ec = stream_.socket().cancel(ec);

  if (ec && ec != beast::errc::not_connected) {
    fail(ec, "close");
  }
}

}  // namespace server::core
