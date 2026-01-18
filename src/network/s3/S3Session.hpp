#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

// --- Boost.Asio Includes ---
#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/stream_file.hpp>

// --- Boost.Beast Includes ---
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/message.hpp>

#include "Config.hpp"
#include "Types.hpp"

namespace hermes::net::s3 {

/**
 * @brief Manages the download of assets from S3-compatible storage.
 * Streams files from S3 to disk in 512KB chunks to minimize memory usage.
 * Uses PartialFileGuard to clean up incomplete downloads on error.
 */
class S3Session : public std::enable_shared_from_this<S3Session> {
 public:
  static std::expected<std::shared_ptr<S3Session>, hermes::config::ErrorInfo>
  Create(boost::asio::io_context& ioc,
         const hermes::config::S3Config& manual_cfg = {});

  /**
   * @brief Downloads a file from S3 to local disk.
   * @param file_key The S3 object key to download
   */
  boost::asio::awaitable<std::expected<void, hermes::config::ErrorInfo>>
  RequestFile(std::string file_key);

 private:
  /**
   * @brief Constructs the S3 session.
   *
   * @param ioc The io_context this session's I/O will run on.
   * @param cfg The S3 configuration (defaults to a local MinIO).
   */
  explicit S3Session(boost::asio::io_context& ioc,
                     hermes::config::S3Config cfg);

  /**
   * @brief The main coroutine orchestrating the download logic.
   */
  boost::asio::awaitable<void> DoDownloadFile(std::string file_key);

  /**
   * @brief Creates and signs the S3 GET request.
   * @param file_key The object key to request.
   * @return A signed http::request.
   */
  boost::beast::http::request<boost::beast::http::empty_body> BuildDownloadRequest(
      const std::string& file_key) const;

  // --- Coroutine Helpers ---

  /**
   * @brief Resolves S3 host and connects the TCP socket.
   */
  boost::asio::awaitable<std::expected<void, hermes::config::ErrorInfo>> Connect();

  /**
   * @brief Writes the HTTP request and reads/parses the HTTP response headers.
   */
  boost::asio::awaitable<std::expected<
      std::pair<size_t, boost::beast::flat_buffer>, hermes::config::ErrorInfo>>
  WriteRequestAndReadHeaders(const std::string& file_key);

  /**
   * @brief Creates local directories and opens the destination file for
   * writing.
   */
  std::expected<std::pair<boost::asio::stream_file, std::filesystem::path>,
                hermes::config::ErrorInfo>
  PrepareLocalFile(const std::string& file_key);

  /**
   * @brief The core streaming loop.
   */
  boost::asio::awaitable<std::expected<size_t, hermes::config::ErrorInfo>>
  StreamBodyToFile(boost::asio::stream_file& file, size_t expected_size,
                      boost::beast::flat_buffer& header_buffer);

  /**
   * @brief Performs a graceful shutdown and close of the socket.
   */
  void CleanupSocket();

  boost::asio::io_context& ioc_;
  hermes::config::S3Config cfg_;
  boost::asio::ip::tcp::resolver resolver_;
  boost::beast::tcp_stream stream_;
};
}  // namespace hermes::net::s3
