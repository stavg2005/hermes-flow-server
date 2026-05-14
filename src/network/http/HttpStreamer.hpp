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
#include <vector>

#include "Concepts.hpp"
#include "Types.hpp"

namespace hermes::net::http {

/**
 * @class HttpStreamer
 * @brief Generic asynchronous HTTP client for streaming responses to sinks.
 *
 * HttpStreamer abstracts away connection management, HTTP packetizing, and
 * error handling. It allows any HTTP request to be executed, with the
 * response body being directly piped to any valid Boost Asio AsyncWriteStream
 * (e.g., FileSink, TCP sockets). Designed as a header-only template class.
 */
class HttpStreamer {
 public:
  static constexpr auto NETWORK_TIMEOUT = std::chrono::seconds(30);
  static constexpr size_t MAX_HTTP_BODY_SIZE =
      2ULL * 1024 * 1024 * 1024;  // 2GB Limit

  // OPTIMIZED: Reduced to 128KB to align with DSP block sizes and fit nicely on
  // the coroutine frame
  static constexpr size_t TRANSFER_CHUNK_SIZE = 128 * 1024;

  /**
   * @brief Constructs an HttpStreamer attached to an execution context.
   * @param ioc The ASIO IO Context to run network operations on.
   */
  explicit HttpStreamer(boost::asio::io_context& ioc)
      : ioc_(ioc), resolver_(ioc), stream_(ioc) {}

  /**
   * @brief Executes a generic HTTP request and streams the response to a sink.
   *
   * @tparam Body The Boost.Beast body type (e.g. empty_body, string_body).
   * @tparam Fields The container type for HTTP headers.
   * @tparam Sink A destination satisfying the AsyncWriteStream concept.
   * @param host The target hostname or IP address.
   * @param port The target port number as a string.
   * @param req The pre-configured HTTP request to send.
   * @param destination The stream (file, socket, memory) to write the response
   * body into.
   * @return std::expected<void, config::ErrorInfo> Void on success, or an
   * ErrorInfo detailing the network failure.
   */
  template <typename Body, typename Fields, config::AsyncWriteStream Sink>
  boost::asio::awaitable<std::expected<void, config::ErrorInfo>> execute(
      const std::string& host, const std::string& port,
      boost::beast::http::request<Body, Fields> req, Sink& destination) {
    stream_.expires_after(NETWORK_TIMEOUT);
    auto connect_res = co_await connect(host, port);
    if (!connect_res) co_return std::unexpected(connect_res.error());

    stream_.expires_after(NETWORK_TIMEOUT);

    boost::system::error_code ec;
    co_await boost::beast::http::async_write(
        stream_, req,
        boost::asio::redirect_error(boost::asio::use_awaitable, ec));
    if (ec)
      co_return std::unexpected(
          config::ErrorInfo::From(config::AppError::NetworkError,
                                  "Write request failed: " + ec.message()));

    boost::beast::flat_buffer buffer;
    boost::beast::http::response_parser<boost::beast::http::empty_body> parser;
    parser.body_limit(MAX_HTTP_BODY_SIZE);

    co_await boost::beast::http::async_read_header(
        stream_, buffer, parser,
        boost::asio::redirect_error(boost::asio::use_awaitable, ec));
    if (ec)
      co_return std::unexpected(
          config::ErrorInfo::From(config::AppError::NetworkError,
                                  "Read header failed: " + ec.message()));

    if (parser.get().result() != boost::beast::http::status::ok) {
      co_return co_await handle_error_response(std::move(parser), buffer);
    }

    auto expected_size = get_content_length(parser.get());

    stream_.expires_never();
    co_return co_await stream_to_sink(destination, buffer, expected_size);
  }

 private:
  boost::asio::io_context& ioc_;
  boost::asio::ip::tcp::resolver resolver_;
  boost::beast::tcp_stream stream_;

  /**
   * @brief Resolves the hostname and establishes a TCP connection.
   */
  boost::asio::awaitable<std::expected<void, config::ErrorInfo>> connect(
      const std::string& host, const std::string& port) {
    boost::system::error_code ec;
    auto results = co_await resolver_.async_resolve(
        host, port,
        boost::asio::redirect_error(boost::asio::use_awaitable, ec));
    if (ec) {
      co_return std::unexpected(config::ErrorInfo::From(
          config::AppError::NetworkError, "Resolve failed: " + ec.message()));
    }

    co_await stream_.async_connect(
        results, boost::asio::redirect_error(boost::asio::use_awaitable, ec));
    if (ec) {
      co_return std::unexpected(config::ErrorInfo::From(
          config::AppError::NetworkError, "Connect failed: " + ec.message()));
    }
    co_return std::expected<void, config::ErrorInfo>();
  }

  /**
   * @brief Drains an HTTP error response body to extract and log the failure
   * message.
   */
  template <typename Parser>
  boost::asio::awaitable<std::unexpected<config::ErrorInfo>>
  handle_error_response(Parser&& parser, boost::beast::flat_buffer& buffer) {
    boost::beast::http::response_parser<boost::beast::http::string_body>
        error_parser(std::move(parser));

    boost::system::error_code ec;
    co_await boost::beast::http::async_read(
        stream_, buffer, error_parser,
        boost::asio::redirect_error(boost::asio::use_awaitable, ec));

    std::string error_body = "No Body";
    if (!ec) {
      error_body = error_parser.get().body();
    } else if (ec != boost::asio::error::eof &&
               ec != boost::beast::http::error::end_of_stream) {
      error_body = "Failed to parse error body: " + ec.message();
    }

    int status = error_parser.get().result_int();

    spdlog::error("HTTP Request Failed [{}]: {}", status, error_body);
    co_return std::unexpected(config::ErrorInfo::From(
        config::AppError::NetworkError,
        "HTTP Error " + std::to_string(status) + ": " + error_body));
  }

  size_t get_content_length(
      const boost::beast::http::response_header<>& header) {
    if (auto it = header.find(boost::beast::http::field::content_length);
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
  boost::asio::awaitable<std::expected<size_t, config::ErrorInfo>>
  drain_residual_buffer(Sink& sink, boost::beast::flat_buffer& buffer) {
    size_t written = 0;
    if (buffer.size() > 0) {
      boost::system::error_code ec;
      written = co_await boost::asio::async_write(
          sink, buffer.data(),
          boost::asio::redirect_error(boost::asio::use_awaitable, ec));
      if (ec) {
        co_return std::unexpected(config::ErrorInfo::From(
            config::AppError::NetworkError, "Drain error: " + ec.message()));
      }
      buffer.consume(buffer.size());
    }
    co_return written;
  }

  /**
   * @brief Main transfer loop. Reads from the TCP stream and writes to the Sink
   * until EOF or target size.
   */
  template <config::AsyncWriteStream Sink>
  boost::asio::awaitable<std::expected<size_t, config::ErrorInfo>>
  transfer_stream_data(Sink& sink, size_t remaining_size) {
    size_t total_written = 0;

    // OPTIMIZED: Allocated directly on the coroutine frame.
    // Boost.Asio guarantees this memory remains valid across suspension points.
    // This entirely eliminates the severe heap fragmentation caused by
    // std::make_shared.
    std::vector<uint8_t> buffer(TRANSFER_CHUNK_SIZE);

    while (remaining_size == 0 || total_written < remaining_size) {
      boost::system::error_code ec;
      size_t bytes_read = co_await stream_.async_read_some(
          boost::asio::buffer(buffer),
          boost::asio::redirect_error(boost::asio::use_awaitable, ec));

      if (ec == boost::asio::error::eof) break;
      if (ec)
        co_return std::unexpected(config::ErrorInfo::From(
            config::AppError::NetworkError, "Read error: " + ec.message()));
      if (bytes_read == 0) break;

      size_t written = co_await boost::asio::async_write(
          sink, boost::asio::buffer(buffer, bytes_read),
          boost::asio::redirect_error(boost::asio::use_awaitable, ec));

      if (ec)
        co_return std::unexpected(config::ErrorInfo::From(
            config::AppError::NetworkError, "Write error: " + ec.message()));

      total_written += written;
    }

    co_return total_written;
  }

  /**
   * @brief Orchestrates the draining and streaming phases to fulfill the HTTP
   * body transfer.
   */
  template <config::AsyncWriteStream Sink>
  boost::asio::awaitable<std::expected<void, config::ErrorInfo>> stream_to_sink(
      Sink& sink, boost::beast::flat_buffer& buffer, size_t expected_size) {
    size_t total_written = 0;

    auto drain_res = co_await drain_residual_buffer(sink, buffer);
    if (!drain_res) co_return std::unexpected(drain_res.error());
    total_written += *drain_res;

    if (expected_size == 0 || total_written < expected_size) {
      size_t remaining =
          (expected_size > 0) ? (expected_size - total_written) : 0;
      auto transfer_res = co_await transfer_stream_data(sink, remaining);
      if (!transfer_res) co_return std::unexpected(transfer_res.error());
      total_written += *transfer_res;
    }

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
