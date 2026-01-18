#include "S3Session.hpp"

#include <algorithm>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/stream_file.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/http.hpp>
#include <ctime>
#include <exception>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "BufferPool.hpp"
#include "PartialFileGuard.hpp"
#include "S3RequestFactory.hpp"
#include "Types.hpp"
#include "spdlog/spdlog.h"

namespace hermes::net::s3 {
// ============================================================================
// Constants & Configuration
// ============================================================================

static constexpr size_t KILOBYTE = 1024;
static constexpr size_t MEGABYTE = 1024 * KILOBYTE;

// Buffer size for reading from the socket. 512KB is a good balance for
// throughput.
static constexpr size_t DEFAULT_CHUNK_SIZE = 512 * KILOBYTE;

// Max allowable body size (2 GB) to prevent memory exhaustion attacks.
static constexpr uint64_t MAX_BODY_LIMIT = 2ULL * 1024 * 1024 * 1024;

// Frequency of progress logging (e.g., every 100 MB)
static constexpr size_t PROGRESS_LOG_MB = 100;

// ============================================================================
// Helper Methods
// ============================================================================

http::request<http::empty_body> S3Session::BuildDownloadRequest(
    const std::string& file_key) const {
  spdlog::debug("service is {}", cfg_.service);
  return S3RequestFactory::create_signed_get_request(cfg_, http::verb::get,
                                                     file_key);
}

asio::awaitable<std::expected<void, ErrorInfo>> S3Session::Connect() {
  spdlog::debug("attempting to connect to minio with {} {}", cfg_.host,
                cfg_.port);
  try {
    auto results = co_await resolver_.async_resolve(cfg_.host, cfg_.port,
                                                    asio::use_awaitable);

    co_await asio::async_connect(stream_.socket(), results,
                                 asio::use_awaitable);

    spdlog::debug("Connected to S3: {}:{}", cfg_.host, cfg_.port);
    co_return std::expected<void, ErrorInfo>();  // Success
  } catch (const boost::system::system_error& e) {
    co_return std::unexpected(ErrorInfo::From(
        AppError::NetworkError, "Connection failed: " + std::string(e.what())));
  }
}

asio::awaitable<std::expected<std::pair<size_t, beast::flat_buffer>, ErrorInfo>>
S3Session::WriteRequestAndReadHeaders(const std::string& file_key) {
  try {
    auto req = BuildDownloadRequest(file_key);
    co_await http::async_write(stream_, req, asio::use_awaitable);

    beast::flat_buffer header_buffer;

    // Use empty_body to avoid buffering the full file in RAM.
    http::response_parser<http::empty_body> parser;
    parser.body_limit(MAX_BODY_LIMIT);

    co_await http::async_read_header(stream_, header_buffer, parser,
                                     asio::use_awaitable);

    const auto& response = parser.get();

    if (response.result() != http::status::ok) {
      // If error, drain the rest of the body to get the error message XML/JSON
      http::response_parser<http::string_body> error_parser(std::move(parser));
      co_await http::async_read(stream_, header_buffer, error_parser,
                                asio::use_awaitable);

      spdlog::error("S3 Request Failed [{}]: {}", response.result_int(),
                    error_parser.get().body());

      co_return std::unexpected(ErrorInfo::From(
          AppError::NetworkError, "S3 HTTP Error " +
                                      std::to_string(response.result_int()) +
                                      ": " + error_parser.get().body()));
    }

    size_t expected_size = 0;
    if (auto it = response.find(http::field::content_length);
        it != response.end()) {
      expected_size = std::stoull(std::string(it->value()));

      spdlog::info("Downloading {} ({:.2f} MB)", file_key,
                   expected_size / (double)MEGABYTE);
    } else {
      spdlog::warn("S3 response missing Content-Length. Progress unknown.");
    }

    co_return std::make_pair(expected_size, std::move(header_buffer));
  } catch (const std::exception& e) {
    co_return std::unexpected(
        ErrorInfo::From(AppError::NetworkError,
                        "Header exchange failed: " + std::string(e.what())));
  }
}

std::expected<std::pair<asio::stream_file, std::filesystem::path>, ErrorInfo>
S3Session::PrepareLocalFile(const std::string& file_key) {
  const std::filesystem::path local_dir = "downloads";
  const auto filename = std::filesystem::path(file_key).filename();
  std::filesystem::path local_path = local_dir / filename;

  // Ensure directory exists
  std::error_code dir_ec;
  std::filesystem::create_directories(local_dir, dir_ec);
  if (dir_ec) {
    return std::unexpected(
        ErrorInfo::From(AppError::FileSystemError,
                        "Failed to create directory: " + dir_ec.message()));
  }

  // Open file with Truncate (overwrite) mode
  asio::stream_file file(ioc_);
  beast::error_code ec;
  file.open(local_path.string(),
            asio::stream_file::write_only | asio::stream_file::create |
                asio::stream_file::truncate,
            ec);

  if (ec) {
    return std::unexpected(
        ErrorInfo::From(AppError::FileSystemError,
                        "Failed to open local file: " + local_path.string()));
  }

  return std::make_pair(std::move(file), std::move(local_path));
}

asio::awaitable<std::expected<size_t, ErrorInfo>>
S3Session::StreamBodyToFile(asio::stream_file& file, size_t expected_size,
                               beast::flat_buffer& header_buffer) {
  size_t total_written = 0;
  size_t last_logged_mb = 0;

  try {
    // Write any body bytes pulled into 'header_buffer' during header read
    if (header_buffer.size() > 0) {
      total_written += co_await asio::async_write(file, header_buffer.data(),
                                                  asio::use_awaitable);
      header_buffer.consume(header_buffer.size());
    }

    // Stream the Rest
    auto shared_buf = infra::BufferPool::Instance().Acquire(DEFAULT_CHUNK_SIZE);
    std::vector<uint8_t>& buf = *shared_buf;

    while (expected_size == 0 || total_written < expected_size) {
      size_t remaining = (expected_size > 0) ? (expected_size - total_written)
                                             : DEFAULT_CHUNK_SIZE;
      size_t bytes_to_request = std::min(DEFAULT_CHUNK_SIZE, remaining);

      if (bytes_to_request == 0) break;

      try {
        size_t bytes_read = co_await stream_.async_read_some(
            asio::buffer(buf.data(), bytes_to_request), asio::use_awaitable);

        if (bytes_read == 0) break;

        total_written += co_await asio::async_write(
            file, asio::buffer(buf, bytes_read), asio::use_awaitable);

        size_t current_mb = total_written / MEGABYTE;
        if (current_mb >= last_logged_mb + PROGRESS_LOG_MB) {
          spdlog::info("... {} MB downloaded", current_mb);
          last_logged_mb = current_mb;
        }

      } catch (const boost::system::system_error& e) {
        if (e.code() == asio::error::eof) {
          break;
        }
        throw;  // Re-throw to outer catch block
      }
    }

    if (expected_size > 0 && total_written != expected_size) {
      spdlog::error("S3 Download failed: Size mismatch (Expected: {}, Got: {})",
                    expected_size, total_written);
      co_return std::unexpected(ErrorInfo::From(
          AppError::NetworkError,
          "S3 Download truncated: Expected " + std::to_string(expected_size) +
              " bytes, but received " + std::to_string(total_written)));
    }

    co_return total_written;

  } catch (const std::exception& e) {
    co_return std::unexpected(
        ErrorInfo::From(AppError::NetworkError,
                        "Streaming body failed: " + std::string(e.what())));
  }
}

boost::asio::awaitable<std::expected<void, ErrorInfo>> S3Session::RequestFile(
    std::string file_key) {
  spdlog::info("[S3] Initiating request for: {}", file_key);

  stream_.expires_never();

  auto connect_res = co_await Connect();
  if (!connect_res) co_return std::unexpected(connect_res.error());

  auto header_res = co_await WriteRequestAndReadHeaders(file_key);
  if (!header_res) co_return std::unexpected(header_res.error());

  auto [expected_size, header_buf] = std::move(*header_res);

  auto file_res = PrepareLocalFile(file_key);
  if (!file_res) co_return std::unexpected(file_res.error());

  auto [file, path] = std::move(*file_res);

  PartialFileGuard guard(path);

  auto stream_res =
      co_await StreamBodyToFile(file, expected_size, header_buf);

  if (!stream_res) {
    co_return std::unexpected(stream_res.error());
  }

  file.close();
  guard.disarm();

  spdlog::info("[S3] Download Complete: {}", file_key);
  co_return std::expected<void, ErrorInfo>();
}

S3Session::S3Session(boost::asio::io_context& ioc, S3Config cfg)
    : ioc_(ioc), cfg_(std::move(cfg)), resolver_(ioc), stream_(ioc) {}

std::expected<std::shared_ptr<S3Session>, ErrorInfo> S3Session::Create(
    boost::asio::io_context& ioc, const S3Config& manual_cfg) {
  S3Config final_config = manual_cfg;

  if (final_config.access_key.empty()) {
    auto config_result = LoadConfig("../config.toml");

    if (!config_result) {
      spdlog::error("S3Session failed to load config: {}",
                    config_result.error().message);
      return std::unexpected(config_result.error());
    }

    final_config = std::move(config_result.value().s3);
  }
  return std::shared_ptr<S3Session>(
      new S3Session(ioc, std::move(final_config)));
}
};  // namespace hermes::net::s3
