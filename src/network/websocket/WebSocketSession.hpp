#pragma once
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/json.hpp>
#include <memory>
#include <string>
#include <vector>

#include "spdlog/spdlog.h"
#include "types.hpp"

class WebSocketSession : public std::enable_shared_from_this<WebSocketSession> {
    websocket::stream<beast::tcp_stream> ws_;
    beast::flat_buffer buffer_;
    std::vector<std::shared_ptr<std::string>> send_queue_;
    bool is_initialized_ = false;

   public:
    explicit WebSocketSession(tcp::socket&& socket) : ws_(std::move(socket)) {}


    template <class Body, class Allocator>
    void do_accept(http::request<Body, http::basic_fields<Allocator>> req) {
        ws_.async_accept(
            req, beast::bind_front_handler(&WebSocketSession::on_accept, shared_from_this()));
    }

    void on_accept(beast::error_code ec) {
        if (ec) return spdlog::error("WS Accept failed: {}", ec.message());
        spdlog::info("WS Connected");
        do_read();
    }


    void do_read() {
        ws_.async_read(buffer_,
                       beast::bind_front_handler(&WebSocketSession::on_read, shared_from_this()));
    }

    void on_read(beast::error_code ec, std::size_t bytes_transferred) {
        if (ec == websocket::error::closed) return;
        if (ec) return spdlog::error("WS Read failed: {}", ec.message());

        // Convert buffer to string for parsing
        std::string payload = beast::buffers_to_string(buffer_.data());

        // Clear buffer immediately so we are ready for next read
        buffer_.consume(buffer_.size());
        spdlog::debug("Received WS message: {}", payload);

        do_read();
    }

    /**
     * @brief Thread-safe, serialized sending mechanism.
     * @details
     *  Boost.Beast is not thread-safe for concurrent writes. Queue outgoing messages
     */
    void Send(std::string message) {
        auto msg_ptr = std::make_shared<std::string>(std::move(message));
        asio::post(ws_.get_executor(), [self = shared_from_this(), msg_ptr]() {
            bool writing = !self->send_queue_.empty();
            self->send_queue_.push_back(msg_ptr);
            if (!writing) self->do_write();
        });
    }

    void do_write() {
        ws_.async_write(asio::buffer(*send_queue_.front()),
                        beast::bind_front_handler(&WebSocketSession::on_write, shared_from_this()));
    }

    void on_write(beast::error_code ec, std::size_t) {
        if (ec) return;
        send_queue_.erase(send_queue_.begin());
        if (!send_queue_.empty()) do_write();
    }

    /**
     * @brief   Initiates close handshake. Causes the read loop to exit.
     */
    void Close() {
        // Post the close operation to the socket's strand/executor
        // to avoid race conditions with ongoing reads/writes.
        asio::post(ws_.get_executor(), [self = shared_from_this()]() {

            self->ws_.async_close(websocket::close_code::normal, [self](beast::error_code ec) {
                if (ec) spdlog::debug("WS Close: {}", ec.message());
            });
        });
    }
};
