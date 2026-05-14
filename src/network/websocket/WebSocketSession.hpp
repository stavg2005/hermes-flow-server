#pragma once

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <deque>
#include <memory>
#include <string>

namespace hermes::net::websocket {

/**
 * @class WebSocketSession
 * @brief Manages a single WebSocket connection.
 * * Handles serialized asynchronous reading and writing to ensure thread safety
 * when communicating with the connected client.
 */
class WebSocketSession : public std::enable_shared_from_this<WebSocketSession> {
 public:
  /**
   * @brief Constructs a new WebSocketSession.
   * @param socket The underlying TCP socket (takes ownership via move).
   */
  explicit WebSocketSession(boost::asio::ip::tcp::socket&& socket);

  /**
   * @brief Performs the asynchronous WebSocket handshake.
   * * Note: Implementation remains in the header because it is a template method.
   * @tparam Body The HTTP body type of the upgrade request.
   * @tparam Allocator The HTTP allocator type.
   * @param req The incoming HTTP upgrade request.
   */
  template <class Body, class Allocator>
  void do_accept(boost::beast::http::request<Body, boost::beast::http::basic_fields<Allocator>> req) {
    ws_.async_accept(req,
                     boost::beast::bind_front_handler(&WebSocketSession::on_accept,
                                                      shared_from_this()));
  }

  /**
   * @brief Thread-safe, serialized sending mechanism.
   * Posts the write operation to the strand/executor to prevent concurrent writes.
   * @param message The string payload to send to the client.
   */
  void send(std::string message);

  /**
   * @brief Initiates the WebSocket close handshake safely.
   */
  void close();

 private:
  // Core Async Handlers
  void on_accept(boost::beast::error_code ec);

  void do_read();
  void on_read(boost::beast::error_code ec, std::size_t bytes_transferred);

  void do_write();
  void on_write(boost::beast::error_code ec, std::size_t);

  // Member Variables
  boost::beast::websocket::stream<boost::beast::tcp_stream> ws_;
  boost::beast::flat_buffer buffer_;

  /// Queue ensuring that multiple send() calls are serialized properly
  std::deque<std::shared_ptr<std::string>> send_queue_;

  bool is_initialized_ = false;
};

}  // namespace hermes::net::websocket
