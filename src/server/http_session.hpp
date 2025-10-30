#pragma once
#include "Listener.hpp"
#include "Router.hpp"
#include "ServerContext.hpp"
#include "boost/asio/io_context.hpp"
#include "boost/beast/http/string_body_fwd.hpp"
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <cstddef>
#include <memory>
#include <sys/stat.h>

namespace server::core {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class HttpSession : public std::enable_shared_from_this<HttpSession> {
public:
  explicit HttpSession(tcp::socket &&socket, boost::asio::io_context &io,
                       std::shared_ptr<Router> &router);
  void run();

private:
  // Request handling
  void read_request_header();
  void handle_request();
  void handle_string_request();
  void handle_options_request();
  // Response handling
  void send_response(http::response<http::string_body> &&res);
  void on_write(bool close,
                std::shared_ptr<http::response<http::string_body>> sp,
                beast::error_code ec, std::size_t bytes_transferred);

  // Connection management
  void do_graceful_close();
  void do_close();
  void reset_for_next_request();
  bool is_options_request() const;
  // Helper methods

  http::request<http::string_body>
  convert_to_string_body(const http::request<http::empty_body> &empty_req) {

    http::request<http::string_body> string_req;

    // Copy metadata
    string_req.method(empty_req.method());
    string_req.target(empty_req.target());
    string_req.version(empty_req.version());
    string_req.keep_alive(empty_req.keep_alive());

    // Copy headers using string names (safer)
    for (auto const &field : empty_req) {
      std::string field_name{field.name_string()};
      std::string field_value{field.value()};

      // Use string-based set method which is more forgiving
      string_req.set(field_name, field_value);
    }

    // Empty body
    string_req.body() = "";
    string_req.prepare_payload();

    return string_req;
  }

private:
  beast::tcp_stream stream_;
  beast::flat_buffer buffer_;
  std::shared_ptr<http::request_parser<http::string_body>> parser_;
  std::shared_ptr<Router> router_;
};

} // namespace server::core