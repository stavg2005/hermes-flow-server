#pragma once

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/beast.hpp>
#include <memory>

#include "Router.hpp"
#include "types.hpp"

namespace server::core {

/**
 * @brief Handles a single HTTP connection.
 * Manages the lifecycle of the socket until it is upgraded (WebSocket) or closed.
 */
class HttpSession : public std::enable_shared_from_this<HttpSession> {
   public:
    HttpSession(tcp::socket&& socket, const std::shared_ptr<Router>& router);

    // Entry point: launches the session coroutine
    void run();

   private:
    // Core Logic
    asio::awaitable<void> do_session();
    asio::awaitable<void> do_graceful_close();

    // I/O Helpers
    asio::awaitable<std::tuple<beast::error_code, bool>> do_read_request();
    asio::awaitable<beast::error_code> do_write_response(http::response<http::string_body>& res);

    // Logic Helpers
    http::response<http::string_body> do_build_response();
    bool is_options_request() const;
    void do_close();

    beast::tcp_stream stream_;
    std::shared_ptr<Router> router_;
    beast::flat_buffer buffer_;

    // We use std::optional to hold the parser.
    // This allows us to "reset" the parser state by reconstructing it in-place (.emplace())
    // for each new request, without the overhead of heap allocation (new/delete)
    // that a std::shared_ptr or std::unique_ptr would incur.
    std::optional<http::request_parser<http::string_body>> parser_;
    // Acts as a state machine that consumes raw bytes from the socket stream,
    // handles HTTP wire-format complexities (like chunked encoding and
    // header/body separation), and incrementally builds the structured http::request object.
};

}  // namespace server::core
