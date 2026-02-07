// HttpStreamer.hpp
#pragma once

#include <spdlog/spdlog.h>

#include <algorithm>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <exception>
#include <expected>
#include <string>
#include <utility>

#include "BufferPool.hpp"
#include "Concepts.hpp"
#include "Types.hpp"

namespace hermes::net::http {

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;
using tcp = asio::ip::tcp;

class HttpStreamer {
 public:
  explicit HttpStreamer(asio::io_context& ioc)
      : ioc_(ioc), resolver_(ioc), stream_(ioc) {}

  template <typename Body, typename Fields, config::AsyncWriteStream Sink>
  asio::awaitable<std::expected<void, config::ErrorInfo>> execute(
      const std::string& host, const std::string& port,
      http::request<Body, Fields> req, Sink& destination) {
    try {
      stream_.expires_after(std::chrono::seconds(30));
      auto connect_res = co_await connect(host, port);
      if (!connect_res) co_return std::unexpected(connect_res.error());
      spdlog::debug("after connect");

      stream_.expires_after(std::chrono::seconds(30));
      co_await http::async_write(stream_, req, asio::use_awaitable);
      spdlog::debug("after write");
      beast::flat_buffer buffer;
      http::response_parser<http::empty_body> parser;
      parser.body_limit(2ULL * 1024 * 1024 * 1024);  // 2GB Limit

      co_await http::async_read_header(stream_, buffer, parser,
                                       asio::use_awaitable);
      spdlog::debug("after read headers");
      if (parser.get().result() != http::status::ok) {
        co_return co_await handle_error_response(std::move(parser), buffer);
      }

      auto expected_size = get_content_length(parser.get());

      stream_.expires_never();
      co_return co_await stream_to_sink(destination, buffer, expected_size);
    } catch (std::exception& ex) {
      spdlog::debug("exception in httpstreamer {}", ex.what());
      co_return std::unexpected<config::ErrorInfo>(
          config::ErrorInfo::From(config::AppError::NetworkError, ex.what()));
    }
  }

 private:
  asio::io_context& ioc_;
  tcp::resolver resolver_;
  beast::tcp_stream stream_;

  asio::awaitable<std::expected<void, config::ErrorInfo>> connect(
      const std::string& host, const std::string& port) {
    try {
      auto results =
          co_await resolver_.async_resolve(host, port, asio::use_awaitable);
      co_await stream_.async_connect(results, asio::use_awaitable);
      co_return std::expected<void, config::ErrorInfo>();
    } catch (const boost::system::system_error& e) {
      co_return std::unexpected(
          config::ErrorInfo::From(config::AppError::NetworkError,
                                  "Connect failed: " + std::string(e.what())));
    }
  }

  template <typename Parser>
  asio::awaitable<std::unexpected<config::ErrorInfo>> handle_error_response(
      Parser&& parser, beast::flat_buffer& buffer) {
    // Drain the rest of the body to get the error message
    http::response_parser<http::string_body> error_parser(std::move(parser));
    co_await http::async_read(stream_, buffer, error_parser,
                              asio::use_awaitable);

    std::string error_body = error_parser.get().body();
    int status = error_parser.get().result_int();

    spdlog::error("HTTP Request Failed [{}]: {}", status, error_body);
    co_return std::unexpected(config::ErrorInfo::From(
        config::AppError::NetworkError,
        "HTTP Error " + std::to_string(status) + ": " + error_body));
  }

  size_t get_content_length(const http::response_header<>& header) {
    if (auto it = header.find(http::field::content_length);
        it != header.end()) {
      return std::stoull(std::string(it->value()));
    }
    return 0;
  }

  template <config::AsyncWriteStream Sink>
  asio::awaitable<std::expected<void, config::ErrorInfo>> stream_to_sink(
      Sink& sink, beast::flat_buffer& buffer, size_t expected_size) {
    size_t total_written = 0;

    try {
      // 1. Write initial buffer (header leftovers)
      if (buffer.size() > 0) {
        // Create a copy of the data views because async_write needs them valid
        // Note: buffer.data() is valid as long as buffer isn't modified during
        // write
        size_t written = co_await asio::async_write(sink, buffer.data(),
                                                    asio::use_awaitable);
        total_written += written;
        buffer.consume(buffer.size());
      }

      auto shared_buf = infra::BufferPool::instance().acquire(512 * 1024);

      while (true) {
        if (expected_size > 0 && total_written >= expected_size) {
          break;
        }
        size_t bytes_read = 0;

        // 2. Read from Socket
        bytes_read = co_await stream_.async_read_some(asio::buffer(*shared_buf),
                                                      asio::use_awaitable);

        if (bytes_read == 0) break;

        // 3. Write to File (Sink) - NOW PROTECTED
        size_t written = co_await asio::async_write(
            sink, asio::buffer(*shared_buf, bytes_read), asio::use_awaitable);

        total_written += written;
      }
    } catch (const boost::system::system_error& e) {
      // EOF is normal for reads, but critical for writes or unexpected read
      // errors
      if (e.code() != asio::error::eof) {
        spdlog::error("Stream error: {}", e.what());
        co_return std::unexpected(config::ErrorInfo::From(
            config::AppError::NetworkError,
            "Stream transfer error: " + std::string(e.what())));
      }
    } catch (const std::exception& e) {
      spdlog::error("Critical stream exception: {}", e.what());
      co_return std::unexpected(config::ErrorInfo::From(
          config::AppError::Critical,
          "Critical stream exception: " + std::string(e.what())));
    }

    // Basic validation
    if (expected_size > 0 && total_written != expected_size) {
      co_return std::unexpected(config::ErrorInfo::From(
          config::AppError::NetworkError,
          "Download truncated. Expected: " + std::to_string(expected_size) +
              ", Got: " + std::to_string(total_written)));
    }

    co_return std::expected<void, config::ErrorInfo>();
  }
};
}  // namespace hermes::net::http
