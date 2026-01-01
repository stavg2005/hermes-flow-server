

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
#include "spdlog/spdlog.h"
#include "types.hpp"

// ============================================================================
// Constants & Configuration
// ============================================================================

static constexpr size_t KILOBYTE = 1024;
static constexpr size_t MEGABYTE = 1024 * KILOBYTE;

// Buffer size for reading from the socket. 512KB is a good balance for
// throughput.
static constexpr size_t DEFAULT_CHUNK_SIZE = 512 * KILOBYTE;

// Max allowable body size (2 GB) to prevent memory exhaustion attacks,
// though here we stream to disk, so this limit applies to the parser state.
static constexpr uint64_t MAX_BODY_LIMIT = 2ULL * 1024 * 1024 * 1024;

// Frequency of progress logging (e.g., every 100 MB)
static constexpr size_t PROGRESS_LOG_MB = 100;

// ============================================================================
// Helper Methods
// ============================================================================

http::request<http::empty_body> S3Session::build_download_request(
    const std::string& file_key) const {
    spdlog::debug("service is {}", cfg_.service);
    return S3RequestFactory::create_signed_GET_request(cfg_, http::verb::get, file_key);
}

asio::awaitable<void> S3Session::connect() {
    spdlog::debug("attempting to connect to minio with {} {}", cfg_.host, cfg_.port);
    auto results = co_await resolver_.async_resolve(cfg_.host, cfg_.port, asio::use_awaitable);

    co_await asio::async_connect(stream_.socket(), results, asio::use_awaitable);

    spdlog::debug("Connected to S3: {}:{}", cfg_.host, cfg_.port);
}

asio::awaitable<std::pair<size_t, beast::flat_buffer>> S3Session::write_request_and_read_headers(
    const std::string& file_key) {
    auto req = build_download_request(file_key);
    co_await http::async_write(stream_, req, asio::use_awaitable);

    beast::flat_buffer header_buffer;

    // Use empty_body to avoid buffering the full file in RAM.
    http::response_parser<http::empty_body> parser;
    parser.body_limit(MAX_BODY_LIMIT);

    co_await http::async_read_header(stream_, header_buffer, parser, asio::use_awaitable);

    const auto& response = parser.get();

    if (response.result() != http::status::ok) {
        // If error, drain the rest of the body to get the error message XML/JSON
        http::response_parser<http::string_body> error_parser(std::move(parser));
        co_await http::async_read(stream_, header_buffer, error_parser, asio::use_awaitable);

        spdlog::error("S3 Request Failed [{}]: {}", response.result_int(),
                      error_parser.get().body());
        throw std::runtime_error("S3 Error Code: " + std::to_string(response.result_int()));
    }

    size_t expected_size = 0;
    if (auto it = response.find(http::field::content_length); it != response.end()) {
        expected_size = std::stoull(std::string(it->value()));

        spdlog::info("Downloading {} ({:.2f} MB)", file_key, expected_size / (double)MEGABYTE);
    } else {
        spdlog::warn("S3 response missing Content-Length. Progress unknown.");
    }

    co_return std::make_pair(expected_size, std::move(header_buffer));
}

std::pair<asio::stream_file, std::filesystem::path> S3Session::prepare_local_file(
    const std::string& file_key) {
    const std::filesystem::path local_dir = "downloads";
    const auto filename = std::filesystem::path(file_key).filename();
    std::filesystem::path local_path = local_dir / filename;

    // Ensure directory exists
    std::error_code dir_ec;
    std::filesystem::create_directories(local_dir, dir_ec);

    // Open file with Truncate (overwrite) mode
    asio::stream_file file(ioc_);
    beast::error_code ec;
    file.open(
        local_path.string(),
        asio::stream_file::write_only | asio::stream_file::create | asio::stream_file::truncate,
        ec);

    if (ec) {
        throw boost::system::system_error(ec, "Failed to open local file: " + local_path.string());
    }

    return {std::move(file), std::move(local_path)};
}

asio::awaitable<size_t> S3Session::stream_body_to_file(asio::stream_file& file,
                                                       size_t expected_size,
                                                       beast::flat_buffer& header_buffer) {
    size_t total_written = 0;
    size_t last_logged_mb = 0;

    // The 'async_read_header' call might have pulled some body bytes into
    // 'header_buffer'. We MUST write these to the file first.
    if (header_buffer.size() > 0) {
        total_written +=
            co_await asio::async_write(file, header_buffer.data(), asio::use_awaitable);
        // Mark buffer as consumed so Beast knows we used that data
        header_buffer.consume(header_buffer.size());
    }

    // Stream the Rest
    auto shared_buf = BufferPool::Instance().Acquire(DEFAULT_CHUNK_SIZE);

    std::vector<uint8_t>& buf = *shared_buf;

    while (expected_size == 0 || total_written < expected_size) {
        // Read only remaining bytes to avoid eating into the next request (Keep-Alive).
        size_t remaining =
            (expected_size > 0) ? (expected_size - total_written) : DEFAULT_CHUNK_SIZE;
        size_t bytes_to_request = std::min(DEFAULT_CHUNK_SIZE, remaining);

        if (bytes_to_request == 0) break;

        try {
            // we cant read directly to file from the socket we must use a
            // middle buffer in the ram
            size_t bytes_read = co_await stream_.async_read_some(
                asio::buffer(buf.data(), bytes_to_request), asio::use_awaitable);

            if (bytes_read == 0) break;  // EOF from server

            total_written += co_await asio::async_write(file, asio::buffer(buf, bytes_read),
                                                        asio::use_awaitable);

            size_t current_mb = total_written / MEGABYTE;
            if (current_mb >= last_logged_mb + PROGRESS_LOG_MB) {
                spdlog::info("... {} MB downloaded", current_mb);
                last_logged_mb = current_mb;
            }

        } catch (const boost::system::system_error& e) {
            if (e.code() == asio::error::eof) {
                // Clean disconnect (common if Content-Length matches)
                break;
            }
            throw;  // Real error
        }
    }

    if (expected_size > 0 && total_written != expected_size) {
        spdlog::warn("Size mismatch: Expected {} bytes, Got {} bytes", expected_size,
                     total_written);
    }

    co_return total_written;
}

asio::awaitable<void> S3Session::RequestFile(std::string file_key) {
    try {
        spdlog::info("[S3] Initiating request for: {}", file_key);

        // Disable timeout for the data phase (large files might take time)
        stream_.expires_never();

        co_await connect();

        auto [expected_size, header_buf] = co_await write_request_and_read_headers(file_key);

        auto [file, path] = prepare_local_file(file_key);

        // Download Body (RAII Guard protects incomplete files)
        //  If an exception is thrown, 'guard' will delete the partial file.
        PartialFileGuard guard(path);

        size_t written = co_await stream_body_to_file(file, expected_size, header_buf);

        file.close();
        guard.disarm();

        spdlog::info("[S3] Download Complete: {}", file_key);

    } catch (const std::exception& e) {
        spdlog::error("[S3] Download Failed for '{}': {}", file_key, e.what());

        // Force close socket on error to ensure clean state
        beast::error_code ec;
        stream_.socket().close(ec);

        throw;  // Propagate error to caller
    }
}
