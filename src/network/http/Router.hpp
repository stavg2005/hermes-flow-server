#pragma once
#include <boost/asio/io_context.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/http.hpp>
#include <boost/url/url_view.hpp>
#include <memory>

#include "/ActiveSessions.hpp"
#include "WebSocketSessionObserver.hpp"
#include "boost/beast/core/tcp_stream.hpp"
#include "boost/beast/http/message_fwd.hpp"
#include "boost/beast/http/string_body_fwd.hpp"
#include "io_context_pool.hpp"
#include "response_builder.hpp"
#include "types.hpp"

using ResponseBuilder = server::models::ResponseBuilder;
using req_t = http::request<http::string_body>;
using res_t = http::response<http::string_body>;
class Router {
   public:
    explicit Router(std::shared_ptr<ActiveSessions> active, std::shared_ptr<io_context_pool> pool)
        : active_(std::move(active)), pool_(std::move(pool)) {}

    void RouteQuery(const http::request<http::string_body>& req,
                    http::response<http::string_body>& res, boost::beast::tcp_stream& stream);

   private:
    void Router::handle_websocket_request(const req_t& req, res_t& res,
                                          boost::beast::tcp_stream& stream);
    // Handle transmit requests
    void handle_transmit(const http::request<http::string_body>& req,
                         http::response<http::string_body>& resm);

    void handle_stop(boost::urls::url_view& url, http::response<http::string_body>& res);
    void handle_download(boost::urls::url_view& url, http::response<http::string_body>& res);

    std::shared_ptr<ActiveSessions> active_;
    std::shared_ptr<io_context_pool> pool_;
};
