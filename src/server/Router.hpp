#pragma once
#include <boost/asio/io_context.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/http.hpp>
#include <boost/url/url_view.hpp>
#include <memory>

#include "ActiveSessions.hpp"
#include "boost/beast/http/message_fwd.hpp"
#include "boost/beast/http/string_body_fwd.hpp"
#include "io_context_pool.hpp"
#include "response_builder.hpp"

namespace http = boost::beast::http;

using ResponseBuilder = server::models::ResponseBuilder;

class Router {
 public:
  // Constructor - now takes io_context reference
  explicit Router(std::shared_ptr<ActiveSessions> active)
      : active_(std::move(active)) {}

  void route(const http::request<http::string_body> &req,
             http::response<http::string_body> &res);

  void RouteQuery(const http::request<http::string_body> &req,
                  http::response<http::string_body> &res);

 private:
  // Handle transmit requests
  void handle_transmit(const http::request<http::string_body> &req,
                       http::response<http::string_body> &res);

  void handle_stop(boost::urls::url_view &url,
                   http::response<http::string_body> &res);
  void handle_download(boost::urls::url_view &url,
                       http::response<http::string_body> &res);

  std::shared_ptr<ActiveSessions> active_;
  std::shared_ptr<io_context_pool> pool_;
};
