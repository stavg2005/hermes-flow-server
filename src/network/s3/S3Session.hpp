
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

#include "types.hpp"
#include "config.hpp"

/**
 * @brief Manages a single, "one-shot" connection to S3 to download a file.
 */
class S3Session : public std::enable_shared_from_this<S3Session> {
   public:
    /**
     * @brief Constructs the S3 session.
     *
     * @param ioc The io_context this session's I/O will run on.
     * @param cfg The S3 configuration (defaults to a local MinIO).
     */
    explicit S3Session(asio::io_context& ioc, const S3Config& cfg = {})
        : ioc_(ioc),
          cfg_(cfg),
          resolver_(ioc),
          stream_(ioc)
    {}

    /**
     * @brief Asynchronously requests a file download from S3.
     *
     * This function "fires and forgets" the download coroutine.
     *
     * @param file_key The object key (path) of the file in the S3 bucket.
     */
    asio::awaitable<void> RequestFile(std::string file_key);

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
    http::request<http::empty_body> build_download_request(
        const std::string& file_key) const;  // Marked const

    // --- Coroutine Helpers ---

    /**
     * @brief Resolves S3 host and connects the TCP socket.
     */
    asio::awaitable<void> connect();

  /**
     * @brief Writes the HTTP request and reads/parses the HTTP response headers.
     * @return A pair:
     * 1. The expected Content-Length of the file.
     * 2. The header_buffer (which may contain the start of the body).
     */
    asio::awaitable<std::pair<size_t, beast::flat_buffer>> write_request_and_read_headers(
        const std::string& file_key);

    /**
     * @brief Creates local directories and opens the destination file for writing.
     * @param file_key The S3 key, used to determine the local filename.
     * @return A pair containing the open file handle and its final path.
     * @throws boost::system::system_error on file or directory I/O error.
     */
    std::pair<asio::stream_file, std::filesystem::path> prepare_local_file(
        const std::string& file_key);

    /**
     * @brief The core streaming loop. Writes leftover data, then reads from
     * socket.
     * @param file The open file handle to write to.
     * @param expected_size The expected file size from headers.
     * @param header_buffer The buffer from async_read_header (contains file
     * start).
     * @return Total number of bytes written to disk.
     */
    asio::awaitable<size_t> stream_body_to_file(asio::stream_file& file, size_t expected_size,
                                               beast::flat_buffer& header_buffer);

    /**
     * @brief Performs a graceful shutdown and close of the socket.
     */
    void cleanup_socket();


    void fetch_files();

    // --- Member Variables ---
    asio::io_context& ioc_;  // Must be declared before members that use it
    S3Config cfg_;
    tcp::resolver resolver_;
    beast::tcp_stream stream_;
    // Removed: beast::flat_buffer buffer_;
};
