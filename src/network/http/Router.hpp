#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/beast.hpp>
#include <boost/url/url_view.hpp>
#include <memory>

#include "ActiveSessions.hpp"
#include "WebSocketSessionObserver.hpp"
#include "IoContextPool.hpp"
#include "response_builder.hpp"
#include "Types.hpp"


using req_t = http::request<http::string_body>;
using res_t = http::response<http::string_body>;

class Router {
   public:
    explicit Router(std::shared_ptr<ActiveSessions> active, std::shared_ptr<IoContextPool> pool);

    void RouteQuery(const req_t& req, res_t& res, boost::beast::tcp_stream& stream);

   private:
    // Handlers
    void handle_transmit(const req_t& req, res_t& res);

    /**
     * @brief Upgrades to WebSocket. Socket ownership is moved to WebSocketSession
     *this session will terminate.
     */
    void handle_websocket_request(const req_t& req, res_t& res, boost::beast::tcp_stream& stream);
    void handle_stop(const req_t& req, res_t& res);

    std::shared_ptr<ActiveSessions> active_;
    std::shared_ptr<IoContextPool> pool_;
};
