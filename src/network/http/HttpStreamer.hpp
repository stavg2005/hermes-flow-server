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

      stream_.expires_after(std::chrono::seconds(30));
      co_await http::async_write(stream_, req, asio::use_awaitable);
      beast::flat_buffer buffer;
      http::response_parser<http::empty_body> parser;
      parser.body_limit(2ULL * 1024 * 1024 * 1024);  // 2GB Limit

      co_await http::async_read_header(stream_, buffer, parser,
                                       asio::use_awaitable);
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

  /**
   * @brief Flushes any body data caught in the Beast flat_buffer during header
   * parsing.
   */
  template <config::AsyncWriteStream Sink>
  asio::awaitable<size_t> drain_residual_buffer(Sink& sink,
                                                beast::flat_buffer& buffer) {
    size_t written = 0;
    if (buffer.size() > 0) {
      written =
          co_await asio::async_write(sink, buffer.data(), asio::use_awaitable);
      buffer.consume(buffer.size());
    }
    co_return written;
  }

  /**
   * @brief Main transfer loop. Reads from the TCP stream and writes to the Sink
   * until EOF or target size.
   */
  template <config::AsyncWriteStream Sink>
  asio::awaitable<size_t> transfer_stream_data(Sink& sink,
                                               size_t remaining_size) {
    size_t total_written = 0;
    auto shared_buf =
        std::make_shared<std::vector<uint8_t>>(512 * 1024);  // 512KB chunk

    // If remaining_size is 0, we read until the socket throws EOF (e.g.,
    // chunked transfer).
    while (remaining_size == 0 || total_written < remaining_size) {
      size_t bytes_read = co_await stream_.async_read_some(
          asio::buffer(*shared_buf), asio::use_awaitable);

      if (bytes_read == 0) break;

      size_t written = co_await asio::async_write(
          sink, asio::buffer(*shared_buf, bytes_read), asio::use_awaitable);

      total_written += written;
    }

    co_return total_written;
  }

  template <config::AsyncWriteStream Sink>
  asio::awaitable<std::expected<void, config::ErrorInfo>> stream_to_sink(
      Sink& sink, beast::flat_buffer& buffer, size_t expected_size) {
    size_t total_written = 0;

    try {
      // Step 1: Drain initial buffer (header leftovers)
      total_written += co_await drain_residual_buffer(sink, buffer);

      // Step 2: Stream remaining data directly from socket to sink
      if (expected_size == 0 || total_written < expected_size) {
        size_t remaining =
            (expected_size > 0) ? (expected_size - total_written) : 0;
        total_written += co_await transfer_stream_data(sink, remaining);
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

    // Step 3: Validation
    if (expected_size > 0 && total_written != expected_size) {
      co_return std::unexpected(config::ErrorInfo::From(
          config::AppError::NetworkError,
          std::format("Download truncated. Expected: {}, Got: {}",
                      expected_size, total_written)));
    }

    co_return std::expected<void, config::ErrorInfo>();
  }
};
}  // namespace hermes::net::http
