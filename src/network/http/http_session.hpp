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

    /**
     * @brief HTTP Request Parser.
     * * @note **Optimization Strategy:**
     * We wrap the parser in `std::optional` to control its lifetime manually.
     * Instead of allocating a `new Parser` on the heap for every request
     * (which causes fragmentation over time), we use `.emplace()` to
     * reconstruct the parser **in-place** within the existing `HttpSession` memory footprint.
     * This makes Keep-Alive connections extremely memory-efficient.
     */
    std::optional<http::request_parser<http::string_body>> parser_;

};

}  
