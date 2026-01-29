#include "HttpSession.hpp"

#include <spdlog/spdlog.h>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <chrono>
#include <memory>

#include "Router.hpp"
#include "Types.hpp"

using namespace hermes::infra;
namespace hermes::net::http {

constexpr int SESSION_TIMEOUT_SECONDS = 15;
constexpr int DRAIN_BUFFER_SIZE =
    1024;  // Drain unread TCP data during shutdown
constexpr int DRAIN_TIMEOUT_SECONDS = 1;
void fail(beast::error_code ec, const char* what) {
  if (ec == beast::errc::stream_timeout) {
    spdlog::debug("[HttpSession] Stream timeout: {}", what);
    return;
  }
  if (ec != beast::errc::not_connected && ec != asio::error::eof &&
      ec != asio::error::connection_reset) {
    spdlog::error("[HttpSession] {} error: {}", what, ec.message());
  }
}

std::expected<std::shared_ptr<HttpSession>, ErrorInfo> HttpSession::create(
    tcp::socket socket, std::shared_ptr<Router> router) {
  if (!router) {
    return std::unexpected(ErrorInfo::From(
        AppError::LogicError, "HttpSession requires a valid Router"));
  }


  return std::shared_ptr<HttpSession>(
      new HttpSession(std::move(socket), std::move(router)));
}

HttpSession::HttpSession(tcp::socket&& socket, std::shared_ptr<Router> router)
    : stream_(std::move(socket)), router_(std::move(router)) {}

void HttpSession::run() {
  asio::co_spawn(
      stream_.get_executor(),
      [self = shared_from_this()]() { return self->do_session(); },
      asio::detached);
}
asio::awaitable<void> HttpSession::do_session() {
  // This top-level try-catch is only for unrecoverable infrastructure crashes
  try {
    for (;;) {
      // Reset state
      parser_.emplace();
      parser_->body_limit(1024 * 1024 * 10);  // 10MB limit
      stream_.expires_after(std::chrono::seconds(SESSION_TIMEOUT_SECONDS));


      auto read_result = co_await do_read_request();

      if (!read_result) {
        beast::http::response<beast::http::string_body> err_res;
        ResponseBuilder::build_error_response(
            err_res, "Invalid Request: " + read_result.error().message, 11,
            false, beast::http::status::bad_request);
        //we specificly dont check for error here because if there is an error that means the client can no logner recive messeges  so it dosent matter
        co_await do_write_response(err_res);

        break;
      }

      bool keep_alive = *read_result;
      const auto& req = parser_->get();


      if (beast::websocket::is_upgrade(req)) {
        spdlog::info("Upgrading to WebSocket...");

        beast::http::response<beast::http::string_body> dummy_res;
        router_->route_query(req, dummy_res, stream_);

        co_return;  // End HTTP lifecycle
      }

      auto res = do_build_response();

      auto write_result = co_await do_write_response(res);

      if (!write_result) {
        spdlog::error("[HttpSession] Write failed: {}",
                      write_result.error().message);

        break;
      }


      if (!keep_alive) {
        // Ignore result of close, we are exiting anyway
        co_await do_graceful_close();
        break;
      }
    }
  } catch (const std::exception& e) {
    spdlog::error("[HttpSession] Critical crash: {}", e.what());
  }
}

asio::awaitable<std::expected<bool, ErrorInfo>> HttpSession::do_read_request() {
  beast::error_code ec;


  co_await beast::http::async_read_header(
      stream_, buffer_, *parser_,
      asio::redirect_error(asio::use_awaitable, ec));

  if (ec) {
    if (ec == beast::http::error::end_of_stream) {
      co_return std::unexpected(
          ErrorInfo::From(AppError::NetworkError, "End of Stream"));
    }
    co_return std::expected<bool, ErrorInfo>(
        std::unexpected(ErrorInfo::From(AppError::NetworkError, ec.message())));
  }


  if (!is_options_request()) {
    co_await beast::http::async_read(stream_, buffer_, *parser_,
                              asio::redirect_error(asio::use_awaitable, ec));

    if (ec) {
      co_return std::unexpected(
          ErrorInfo::From(AppError::NetworkError, ec.message()));
    }
  }

  co_return parser_->get().keep_alive();
}

asio::awaitable<std::expected<void, ErrorInfo>> HttpSession::do_write_response(
    beast::http::response<beast::http::string_body>& res) {
  res.prepare_payload();

  // Optimization: Disable Nagle
  beast::error_code ec;
  stream_.socket().set_option(
      tcp::no_delay(true),
      ec);  // Ignore error here, it's optional optimization

  
  co_await beast::http::async_write(stream_, res,
                             asio::redirect_error(asio::use_awaitable, ec));

  if (ec) {
    co_return std::unexpected(
        ErrorInfo::From(AppError::NetworkError, ec.message()));
  }

  co_return std::expected<void, ErrorInfo>{};
}

asio::awaitable<std::expected<void, ErrorInfo>>
HttpSession::do_graceful_close() {
  beast::error_code ec;
  stream_.socket().shutdown(tcp::socket::shutdown_send, ec);

  if (ec && ec != beast::errc::not_connected) {
    // This is technically an error, but we are closing anyway.
    // We'll report it just in case.
    co_return std::unexpected(
        ErrorInfo::From(AppError::NetworkError, ec.message()));
  }


  beast::flat_buffer drain;
  try {
    stream_.expires_after(std::chrono::seconds(DRAIN_TIMEOUT_SECONDS));

    // We use a specific try-catch here because reading during shutdown often
    // throws/errors and we specifically want to ignore it (it's just a drain).
    beast::error_code drain_ec;
    co_await stream_.async_read_some(
        drain.prepare(DRAIN_BUFFER_SIZE),
        asio::redirect_error(asio::use_awaitable, drain_ec));
  } catch (...) {
    // Ignore timeouts or resets during drain
  }

  stream_.socket().close(ec);
  co_return std::expected<void, ErrorInfo>{};
}

bool HttpSession::is_options_request() const {
  return parser_->get().method() == beast::http::verb::options;
}
beast::http::response<beast::http::string_body> HttpSession::do_build_response() {
  beast::http::response<beast::http::string_body> res;
  const auto& req = parser_->get();

  if (is_options_request()) {
    ResponseBuilder::build_options_response(res, req.version(),
                                            req.keep_alive());
  } else {
    res.version(req.version());
    res.keep_alive(req.keep_alive());
    // router fills the response
    router_->route_query(req, res, stream_);
  }
  return res;
}

}  // namespace hermes::net::http
