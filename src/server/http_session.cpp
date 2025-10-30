#include "http_session.hpp"
#include "Listener.hpp"
#include "Router.hpp"
#include "boost/asio/io_context.hpp"
#include "boost/beast/http/message_fwd.hpp"
#include "boost/beast/http/string_body_fwd.hpp"
#include "boost/beast/http/verb.hpp"
#include <chrono>
#include <iostream>
#include <memory>

namespace server::core {

void fail(beast::error_code ec, const char *what) {
  if (ec != beast::errc::not_connected && ec != net::error::eof &&
      ec != net::error::connection_reset) {
    std::cerr << what << ": " << ec.message() << "\n";
  }
}

HttpSession::HttpSession(tcp::socket &&socket, boost::asio::io_context &io,
                         std::shared_ptr<Router> &router)
    : stream_(std::move(socket)) {

  beast::error_code ec;
  auto remote = stream_.socket().remote_endpoint(ec);
  if (!ec) {
    std::cout << "New connection from: " << remote.address() << ":"
              << remote.port() << std::endl;
  }
}

void HttpSession::run() { read_request_header(); }

void HttpSession::read_request_header() {

  parser_ = std::make_shared<http::request_parser<http::string_body>>();

  stream_.expires_after(std::chrono::seconds(30));
  http::async_read_header(
      stream_, buffer_, *parser_,
      [self = shared_from_this()](beast::error_code ec, std::size_t bytes) {
        if (ec == http::error::end_of_stream) {
          return self->do_close();
        }

        if (ec) {
          return fail(ec, "read_header");
        }

        self->handle_request();
      });
}

void HttpSession::handle_request() {
  if (is_options_request()) {
    handle_options_request();
  } else {
    handle_string_request();
  }
}

bool HttpSession::is_options_request() const {
  return parser_->get().method() == http::verb::options;
}

void HttpSession::handle_options_request() {
  auto response = http::response<http::string_body>();
  server::models::ResponseBuilder::build_options_response(
      response, parser_->get().version(), parser_->get().keep_alive());
  send_response(std::move(response));
}

void HttpSession::handle_string_request() {

  stream_.expires_after(std::chrono::seconds(30));
  http::async_read(stream_, buffer_, *parser_,
                   [self = shared_from_this(), this](beast::error_code ec,
                                                     std::size_t bytes) {
                     if (ec) {
                       return fail(ec, "read_body");
                     }

                     const auto &complete_request = parser_->get();

                     std::cout << "Extracted body: " << complete_request.body()
                               << std::endl;

                     http::response<http::string_body> res;
                     res.version(complete_request.version());
                     res.keep_alive(complete_request.keep_alive());

                     std::cout << "Routing" << std::endl;
                     router_->RouteQuery(complete_request, res);
                     send_response(std::move(res));
                   });
}

void HttpSession::send_response(http::response<http::string_body> &&res) {
  auto sp = std::make_shared<http::response<http::string_body>>(std::move(res));

  bool should_close = sp->need_eof();
  std::cout << "should i close? " << should_close << std::endl;

  // Set TCP_NODELAY for immediate sending
  beast::error_code ec;
  stream_.socket().set_option(tcp::no_delay(true), ec);

  http::async_write(stream_, *sp,
                    beast::bind_front_handler(&HttpSession::on_write,
                                              shared_from_this(), should_close,
                                              sp));
}

void HttpSession::on_write(
    bool close, std::shared_ptr<http::response<http::string_body>> /*sp*/,
    beast::error_code ec, std::size_t bytes_transferred) {

  if (ec) {
    return fail(ec, "write");
  }

  std::cout << "Response sent: " << bytes_transferred << " bytes" << std::endl;

  // Only close if the response indicates it should close
  if (close) {
    return do_graceful_close();
  }

  // For keep-alive connections, prepare for next request
  reset_for_next_request();
  read_request_header(); // Start reading next request
}

void HttpSession::reset_for_next_request() {
  buffer_.consume(buffer_.size());
  parser_.reset();
}

void HttpSession::do_graceful_close() {
  std::cout << "graceful closed" << std::endl;
  // Shutdown write side
  beast::error_code ec;
  stream_.socket().shutdown(tcp::socket::shutdown_send, ec);

  // Give client time to receive
  stream_.expires_after(std::chrono::seconds(10));

  // Read FIN PACKET
  auto drain_buffer = std::make_shared<beast::flat_buffer>();
  stream_.async_read_some(drain_buffer->prepare(1024),
                          [self = shared_from_this(), drain_buffer](
                              beast::error_code /*ec*/, std::size_t /*bytes*/) {
                            self->do_close();
                          });
}

void HttpSession::do_close() {
  std::cout << "im closing for real now" << std::endl;
  beast::error_code ec;
  stream_.socket().cancel(ec);
  stream_.socket().close(ec);
}

} // namespace server::core