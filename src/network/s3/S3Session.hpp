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

/**
 * @brief Manages the download of assets from S3-compatible storage.
 * Streams files from S3 to disk in 512KB chunks to minimize memory usage.
 * Uses PartialFileGuard to clean up incomplete downloads on error.
 */
class S3Session : public std::enable_shared_from_this<S3Session> {
   public:
    /**
     * @brief Constructs the S3 session.
     *
     * @param ioc The io_context this session's I/O will run on.
     * @param cfg The S3 configuration (defaults to a local MinIO).
     */
    explicit S3Session(boost::asio::io_context& ioc, const S3Config& cfg = {})
        : ioc_(ioc), cfg_(cfg), resolver_(ioc), stream_(ioc) {
        AppConfig config = load_config("../config.toml");
        cfg_ = std::move(config.s3);
    }

    /**
     * @brief Downloads a file from S3 to local disk.
     * @param file_key The S3 object key to download
     * @throws std::runtime_error if the download fails or file cannot be written
     */
    asio::awaitable<void> request_file(std::string file_key);

   private:
    /**
     * @brief The main coroutine orchestrating the download logic.
     */
    asio::awaitable<void> do_download_file(std::string file_key);

    /**
     * @brief Creates and signs the S3 GET request.
     * @param file_key The object key to request.
     * @return A signed http::request.
     */
    beast::http::request<beast::http::empty_body> build_download_request(
        const std::string& file_key) const;

    // --- Coroutine Helpers ---

    /**
     * @brief Resolves S3 host and connects the TCP socket.
     */
    asio::awaitable<void> connect();

    /**
     * @brief Writes the HTTP request and reads/parses the HTTP response headers.
     */
    asio::awaitable<std::pair<size_t, beast::flat_buffer>> write_request_and_read_headers(
        const std::string& file_key);

    /**
     * @brief Creates local directories and opens the destination file for writing.
     */
    std::pair<asio::stream_file, std::filesystem::path> prepare_local_file(
        const std::string& file_key);

    /**
     * @brief The core streaming loop.
     */
    asio::awaitable<size_t> stream_body_to_file(asio::stream_file& file,
                                                       size_t expected_size,
                                                       beast::flat_buffer& header_buffer);

    /**
     * @brief Performs a graceful shutdown and close of the socket.
     */
    void cleanup_socket();

    asio::io_context& ioc_;
    S3Config cfg_;
    asio::ip::tcp::resolver resolver_;
    beast::tcp_stream stream_;
};
