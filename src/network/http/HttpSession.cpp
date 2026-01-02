#include "HttpSession.hpp"

#include <spdlog/spdlog.h>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <chrono>
#include <memory>

#include "Router.hpp"
#include "Types.hpp"

namespace {

constexpr int SESSION_TIMEOUT_SECONDS = 15;
constexpr int DRAIN_BUFFER_SIZE = 1024;  // Drain unread TCP data during shutdown
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
}  // namespace

namespace server::core {

HttpSession::HttpSession(tcp::socket&& socket, const std::shared_ptr<Router>& router)
    : stream_(std::move(socket)), router_(router) {
    beast::error_code ec;
    auto remote = stream_.socket().remote_endpoint(ec);
    if (!ec) {
        spdlog::debug("New HTTP connection: {}", remote.address().to_string());
    }
}

void HttpSession::run() {
    asio::co_spawn(
        stream_.get_executor(), [self = shared_from_this()]() { return self->do_session(); },
        asio::detached);
}

asio::awaitable<void> HttpSession::do_session() {
    try {
        for (;;) {
            // Reset parser per request (required for HTTP keep-alive)
            parser_.emplace();
            parser_->body_limit(1024 * 1024 * 10);
            stream_.expires_after(std::chrono::seconds(SESSION_TIMEOUT_SECONDS));

            auto [ec_read, keep_alive] = co_await do_read_request();
            if (ec_read) {
                if (ec_read == http::error::end_of_stream) {
                    co_await do_graceful_close();
                } else {
                    fail(ec_read, "read");
                }
                co_return;
            }

            const auto& req = parser_->get();

            if (beast::websocket::is_upgrade(req)) {
                spdlog::info("Upgrading to WebSocket...");
                http::response<http::string_body> dummy_res;

                router_->RouteQuery(req, dummy_res, stream_);

                co_return;
            }

            auto res = do_build_response();

            auto ec_write = co_await do_write_response(res);
            if (ec_write) {
                fail(ec_write, "write");
                co_return;
            }

            if (!keep_alive) {
                co_await do_graceful_close();
                co_return;
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("Session died: {}", e.what());
    }
}

asio::awaitable<std::tuple<beast::error_code, bool>> HttpSession::do_read_request() {
    auto [ec_head, _] = co_await http::async_read_header(stream_, buffer_, *parser_,
                                                         asio::as_tuple(asio::use_awaitable));

    if (ec_head) {
        co_return std::make_tuple(ec_head, false);
    }

    // Read Body (Skip for OPTIONS/GET usually, but parser handles it)
    if (!is_options_request()) {
        // request bening read into the parser
        auto [ec_body, _unused] = co_await http::async_read(stream_, buffer_, *parser_,
                                                            asio::as_tuple(asio::use_awaitable));

        if (ec_body) {
            co_return std::make_tuple(ec_body, false);
        }
    }

    co_return std::make_tuple(beast::error_code{}, parser_->get().keep_alive());
}

http::response<http::string_body> HttpSession::do_build_response() {
    http::response<http::string_body> res;
    const auto& req = parser_->get();

    if (is_options_request()) {
        ResponseBuilder::build_options_response(res, req.version(),
                                                                req.keep_alive());
    } else {
        res.version(req.version());
        res.keep_alive(req.keep_alive());
        // router fills the response
        router_->RouteQuery(req, res, stream_);
    }
    return res;
}

asio::awaitable<beast::error_code> HttpSession::do_write_response(
    http::response<http::string_body>& res) {
    res.prepare_payload();

    // Disable Nagle (TCP_NODELAY) for lower latency
    beast::error_code ec;
    stream_.socket().set_option(tcp::no_delay(true), ec);

    auto [ec_write, bytes] =
        co_await http::async_write(stream_, res, asio::as_tuple(asio::use_awaitable));

    if (!ec_write) {
        spdlog::debug("Sent response: {} bytes", bytes);
    }
    co_return ec_write;
}

asio::awaitable<void> HttpSession::do_graceful_close() {
    beast::error_code ec;
    stream_.socket().shutdown(tcp::socket::shutdown_send, ec);

    if (ec && ec != beast::errc::not_connected) {
        fail(ec, "shutdown");
    }

    // Drain remaining data
    beast::flat_buffer drain;
    try {
        stream_.expires_after(std::chrono::seconds(DRAIN_TIMEOUT_SECONDS));
        co_await stream_.async_read_some(drain.prepare(DRAIN_BUFFER_SIZE), asio::use_awaitable);
    } catch (...) {
        // Expected behavior: Client closes connection or timeout
    }
    do_close();
    co_return;
}

bool HttpSession::is_options_request() const {
    return parser_->get().method() == http::verb::options;
}

void HttpSession::do_close() {
    beast::error_code ec;
    stream_.socket().close(ec);
}

}
