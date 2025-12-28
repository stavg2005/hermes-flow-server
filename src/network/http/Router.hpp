#pragma once

#include <memory>
#include <boost/asio/io_context.hpp>
#include <boost/beast.hpp>
#include <boost/url/url_view.hpp>

#include "ActiveSessions.hpp" // Fixed Include Path
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
    void handle_websocket_request(const req_t& req, res_t& res, boost::beast::tcp_stream& stream);
    void handle_stop(boost::urls::url_view& url, res_t& res);

    std::shared_ptr<ActiveSessions> active_;
    std::shared_ptr<io_context_pool> pool_;
};
