#pragma once
#include "ActiveSessions.hpp"
#include "boost/beast/http/message_fwd.hpp"
#include "boost/beast/http/string_body_fwd.hpp"
#include "boost/smart_ptr/make_unique.hpp"
#include "models/response_builder.hpp"
#include <boost/asio/io_context.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/http.hpp>
#include <boost/url/url_view.hpp>
#include <memory>
#include <string>

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
  void HandleTransmitRequest(const http::request<http::string_body> &req,
                             http::response<http::string_body> &res);

  void HandleStopRequest(boost::urls::url_view url,
                         http::response<http::string_body> &res);

  std::shared_ptr<ActiveSessions> active_;
};