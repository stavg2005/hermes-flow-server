#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/beast.hpp>
#include <boost/url/url_view.hpp>
#include <memory>

#include "ActiveSessions.hpp"  // Fixed Include Path
#include "WebSocketSessionObserver.hpp"
#include "io_context_pool.hpp"
#include "response_builder.hpp"
#include "types.hpp"

using ResponseBuilder = server::models::ResponseBuilder;
using req_t = http::request<http::string_body>;
using res_t = http::response<http::string_body>;

class Router {
   public:
    explicit Router(std::shared_ptr<ActiveSessions> active, std::shared_ptr<io_context_pool> pool);

    void RouteQuery(const req_t& req, res_t& res, boost::beast::tcp_stream& stream);

   private:
    // Handlers
    void handle_transmit(const req_t& req, res_t& res);

    /**
     * @brief Handles the protocol upgrade from HTTP/1.1 to WebSocket.
     * * @note **Socket Ownership Transfer:**
     * Unlike standard HTTP handlers that write a response and close/keep-alive,
     * this method **steals** the underlying TCP socket (`stream`) from the
     * `HttpSession`. It passes the socket to a new `WebSocketSession`, which
     * then manages its own lifetime. The original `HttpSession` coroutine
     * effectively terminates after this call.
     */
    void handle_websocket_request(const req_t& req, res_t& res, boost::beast::tcp_stream& stream);
    void handle_stop(boost::urls::url_view& url, res_t& res);

    std::shared_ptr<ActiveSessions> active_;
    std::shared_ptr<io_context_pool> pool_;
};
