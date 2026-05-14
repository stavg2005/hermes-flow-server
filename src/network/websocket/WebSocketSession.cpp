#include "WebSocketSession.hpp"


#include <spdlog/spdlog.h>

#include <boost/asio/post.hpp>


namespace hermes::net::websocket {

WebSocketSession::WebSocketSession(boost::asio::ip::tcp::socket&& socket)
    : ws_(std::move(socket)) {}

void WebSocketSession::send(std::string message) {
  auto msg_ptr = std::make_shared<std::string>(std::move(message));

  boost::asio::post(ws_.get_executor(), [self = shared_from_this(), msg_ptr]() {
    // If the queue was empty, no write is currently active
    bool writing = !self->send_queue_.empty();
    self->send_queue_.push_back(msg_ptr);

    // Start the write loop if it isn't already running
    if (!writing) {
      self->do_write();
    }
  });
}

void WebSocketSession::close() {
  boost::asio::post(ws_.get_executor(), [self = shared_from_this()]() {
    self->ws_.async_close(boost::beast::websocket::close_code::normal,
                          [self](boost::beast::error_code ec) {
                            if (ec) {
                              spdlog::debug("WS Close: {}", ec.message());
                            }
                          });
  });
}

void WebSocketSession::on_accept(boost::beast::error_code ec) {
  if (ec) {
    spdlog::error("WS Accept failed: {}", ec.message());
    return;
  }
  spdlog::info("WS Connected");
  do_read();
}

void WebSocketSession::do_read() {
  ws_.async_read(buffer_, boost::beast::bind_front_handler(
                              &WebSocketSession::on_read, shared_from_this()));
}

void WebSocketSession::on_read(boost::beast::error_code ec,
                               std::size_t bytes_transferred) {
  if (ec == boost::beast::websocket::error::closed ||
      ec == boost::asio::error::operation_aborted) {
    return;  // Normal teardown
  }

  if (ec) {
    spdlog::error("WS Read failed: {}", ec.message());
    return;
  }

  // Convert buffer to string for processing
  std::string payload = boost::beast::buffers_to_string(buffer_.data());

  // Clear buffer immediately to prepare for the next read
  buffer_.consume(buffer_.size());
  spdlog::debug("Received WS message: {}", payload);

  do_read();
}

void WebSocketSession::do_write() {
  ws_.async_write(boost::asio::buffer(*send_queue_.front()),
                  boost::beast::bind_front_handler(&WebSocketSession::on_write,
                                                   shared_from_this()));
}

void WebSocketSession::on_write(boost::beast::error_code ec, std::size_t) {
  if (ec == boost::beast::websocket::error::closed ||
      ec == boost::asio::error::operation_aborted) {
    return;  // Normal teardown
  }

  if (ec) {
    spdlog::error("WS Write failed: {}", ec.message());
    return;
  }

  // Remove the successfully written message
  send_queue_.pop_front();

  // If there are more messages queued, trigger the next write
  if (!send_queue_.empty()) {
    do_write();
  }
}

}  // namespace hermes::net::websocket
