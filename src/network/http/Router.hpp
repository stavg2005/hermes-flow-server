#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/beast.hpp>
#include <boost/url/url_view.hpp>
#include <expected>  // C++23
#include <memory>

#include "ActiveSessions.hpp"
#include "IoContextPool.hpp"
#include "Types.hpp"
#include "WebSocketSessionObserver.hpp"
#include "response_builder.hpp"
namespace hermes::net::http {
using req_t = beast::http::request<beast::http::string_body>;
using res_t = beast::http::response<beast::http::string_body>;

// Simple structure to hold routing errors without exceptions
struct RouteError {
  beast::http::status code;
  std::string message;
};

class Router {
 public:
  explicit Router(std::shared_ptr<hermes::service::ActiveSessions> active,
                  std::shared_ptr<IoContextPool> pool);

  void route_query(const req_t& req, res_t& res,
                  boost::beast::tcp_stream& stream);

 private:
  // Handlers now return an expected result
  std::expected<void, RouteError> handle_transmit(const req_t& req, res_t& res);

  /**
   * @brief Upgrades to WebSocket. Socket ownership is moved to WebSocketSession
   */
  std::expected<void, RouteError> handle_websocket_request(
      const req_t& req, res_t& res, boost::beast::tcp_stream& stream);

  std::expected<void, RouteError> handle_stop(const req_t& req, res_t& res);

  std::shared_ptr<hermes::service::ActiveSessions> active_;
  std::shared_ptr<IoContextPool> pool_;
};
}  // namespace hermes::net::http
