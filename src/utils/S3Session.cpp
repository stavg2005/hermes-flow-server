
#include "S3Session.hpp"

#include "S3RequestFactory.hpp"

// Core Asio/Beast includes
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/connect.hpp>  // For async_connect
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/stream_file.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http.hpp>

// Standard library includes
#include <ctime>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

// Project-specific includes
#include "PartialFileGuard.hpp"
#include "spdlog/spdlog.h"

// --- Namespaces ---
namespace beast = boost::beast;
namespace net = boost::asio;
namespace http = beast::http;
using tcp = net::ip::tcp;

// --- Constants ---
static constexpr size_t KILOBYTE = 1024.0;
static constexpr size_t MEGABYTE = 1024.0 * KILOBYTE;
static constexpr size_t GIGABYTE = 1024.0 * MEGABYTE;
static constexpr size_t DEFAULT_CHUNK_SIZE = 512 * KILOBYTE;  // 512 KB
static constexpr uint64_t MAX_BODY_LIMIT = 2ULL * GIGABYTE;   // 2 GiB cap
static constexpr size_t PROGRESS_LOG_MB = 100;                // Log every 100MB

// --- build_download_request
http::request<http::empty_body> S3Session::build_download_request(
    const std::string& file_key) const {
    std::time_t now = std::time(nullptr);

    auto get_req = S3RequestFactory::create_signed_GET_request(cfg_, http::verb::get, file_key);
    return get_req;
}

// --- Connect ---
net::awaitable<void> S3Session::connect() {
    auto results = co_await resolver_.async_resolve(cfg_.host, cfg_.port, net::use_awaitable);

    // async_connect will throw on error by default with use_awaitable
    co_await net::async_connect(stream_.socket(), results, net::use_awaitable);
    spdlog::debug("Connected to S3 host: {}", cfg_.host);
}

// --- Write Request & Read Headers ---
net::awaitable<std::pair<size_t, beast::flat_buffer>> S3Session::write_request_and_read_headers(
    const std::string& file_key) {
    auto req = build_download_request(file_key);
    co_await http::async_write(stream_, req, net::use_awaitable);

    // This buffer will be *returned*
    beast::flat_buffer header_buffer;
    http::response_parser<http::empty_body> parser;
    parser.body_limit(MAX_BODY_LIMIT);

    co_await http::async_read_header(stream_, header_buffer, parser, net::use_awaitable);

    const auto& response = parser.get();

    if (response.result() != http::status::ok) {
        // (Error handling logic... unchanged)
        http::response_parser<http::string_body> error_parser(std::move(parser));
        co_await http::async_read(stream_, header_buffer, error_parser, net::use_awaitable);
        std::string error_body = error_parser.get().body();

        spdlog::error("S3 returned error {}: {}", response.result_int(), error_body);
        throw std::runtime_error("S3 non-OK response: " + std::to_string(response.result_int()));
    }

    size_t expected_size = 0;
    if (auto it = response.find(http::field::content_length); it != response.end()) {
        expected_size = std::stoull(std::string(it->value()));
        spdlog::info("Downloading {} ({:.2f} MB)", file_key, expected_size / (1024.0 * 1024.0));
    } else {
        spdlog::warn("S3 response missing Content-Length. Download may be incomplete.");
    }

    // Return both the size and the buffer
    co_return std::make_pair(expected_size, std::move(header_buffer));
}

// This helper prepares the file and returns it. It throws on error.
std::pair<net::stream_file, std::filesystem::path> S3Session::prepare_local_file(
    const std::string& file_key) {
    const std::filesystem::path local_dir = "downloads";
    const auto filename = std::filesystem::path(file_key).filename();
    std::filesystem::path local_path = local_dir / filename;

    // Create directories
    std::error_code dir_ec;
    std::filesystem::create_directories(local_dir, dir_ec);
    if (dir_ec) {
        throw boost::system::system_error(dir_ec, "Failed to create directory");
    }

    // Open file
    net::stream_file file(ioc_);
    beast::error_code ec;
    file.open(local_path.string(),
              net::stream_file::write_only | net::stream_file::create | net::stream_file::truncate,
              ec);
    if (ec) {
        throw boost::system::system_error(ec, "Cannot open local file");
    }
    spdlog::info("local file prepeard");
    return {std::move(file), std::move(local_path)};
}

// Stream Body to File
net::awaitable<size_t> S3Session::stream_body_to_file(net::stream_file& file, size_t expected_size,
                                                      beast::flat_buffer& header_buffer) {
    size_t total_written = 0;
    size_t last_logged_mb = 0;

    // Write the leftover data from the header_buffer first
    if (header_buffer.size() > 0) {
        net::const_buffer to_write = header_buffer.data();
        size_t written = co_await net::async_write(file, to_write, net::use_awaitable);
        total_written += written;
        // Mark the buffer data as "consumed"
        header_buffer.consume(header_buffer.size());
    }

    std::array<std::uint8_t, DEFAULT_CHUNK_SIZE> body_buffer;

    try {
        // Loop *while* we haven't written the expected size
        // (If expected_size is 0, we just read until EOF)
        while (expected_size == 0 || total_written < expected_size) {
            // Calculate how much we *still* need to read.
            size_t remaining =
                (expected_size > 0) ? (expected_size - total_written) : DEFAULT_CHUNK_SIZE;
            if (remaining == 0) {
                break;
            }  // We're done

            // Limit our read to the *smaller* of the buffer size or the remaining
            // amount.
            size_t bytes_to_read = std::min(DEFAULT_CHUNK_SIZE, remaining);

            size_t bytes_read = co_await stream_.async_read_some(
                net::buffer(body_buffer.data(), bytes_to_read), net::use_awaitable);

            if (bytes_read == 0) {
                // This shouldn't happen before EOF, but we treat it as such
                break;
            }

            // Write *only* the bytes we just read
            // span over the body_buffer
            net::const_buffer to_write(body_buffer.data(), bytes_read);

            /* file.async_write_some may not write the entire data so we use the solution boost
             * suggested by using async_write */
            total_written += co_await net::async_write(file, to_write, net::use_awaitable);

            // Log progress
            size_t current_mb = total_written / (1024.0 * 1024.0);
            if (current_mb >= last_logged_mb + PROGRESS_LOG_MB) {
                spdlog::info("... {} MB downloaded", current_mb);
                last_logged_mb = current_mb;
            }
        }
    } catch (const boost::system::system_error& e) {
        if (e.code() == net::error::eof) {
            if (expected_size > 0 && total_written < expected_size) {
                throw std::runtime_error("Connection closed prematurely (Incomplete Download)");
            }
            spdlog::debug("Socket EOF received. Download complete.");

        } else {
            throw;  // Re-throw any other error
        }
    }

    // Check if we got what we expected (if we knew the size)
    if (expected_size > 0 && total_written != expected_size) {
        spdlog::warn("Download finished, but size mismatch. Expected {}, Got {}.", expected_size,
                     total_written);
    }

    co_return total_written;
}

void S3Session::cleanup_socket() {
    beast::error_code ec;  // Ignore errors on cleanup
    stream_.socket().shutdown(tcp::socket::shutdown_both, ec);
    stream_.socket().close(ec);
}

net::awaitable<void> S3Session::do_download_file(std::string file_key) {
    spdlog::info(">>>> [S3 DOWNLOAD] STARTING {} <<<<", file_key);
    stream_.expires_never();
    net::stream_file file(ioc_);
    // if goes out of scope without disarming file would be deleted (RAII pattern)
    std::unique_ptr<PartialFileGuard> file_guard;

    try {
        co_await connect();

        // These will be populated by the helper
        size_t expected_size = 0;
        beast::flat_buffer header_buffer;

        // Step 2: Get headers AND the leftover buffer
        std::tie(expected_size, header_buffer) = co_await write_request_and_read_headers(file_key);

        // Step 3: Prepare local file
        std::filesystem::path local_path;
        std::tie(file, local_path) = prepare_local_file(file_key);

        file_guard = std::make_unique<PartialFileGuard>(std::move(local_path));

        // Step 4: Stream the body, passing in the leftover buffer
        size_t total_written = co_await stream_body_to_file(file, expected_size, header_buffer);

        // (Success and cleanup logic... unchanged)
        beast::error_code ec;
        file.close(ec);
        file_guard->disarm();

        spdlog::info("SUCCESS: Downloaded {} — {} bytes ({:.2f} MB)", file_key, total_written,
                     total_written / (1024.0 * 1024.0));

    } catch (...) {
        cleanup_socket();
        throw;
    }

    cleanup_socket();
    co_return;
}

// --- RequestFile (Entry Point) ---
net::awaitable<void> S3Session::RequestFile(std::string file_key) {
    // Make a copy of the key for the error handler
    auto key_for_handler = file_key;

    try {
        co_await do_download_file(file_key);
    } catch (const std::exception_ptr&
                 p) {  // standarts exceptions cant survive past callback and diffrent threads
        if (p) {
            try {
                // std::exception_ptr is type-erased (opaque). We must rethrow it
                // here to restore its type information so the catch block works.
                std::rethrow_exception(p);
            } catch (const std::exception& e) {
                // This is now the *single* point of failure logging.
                spdlog::error("S3Session failed for file '{}' (host: {}) with error: {}", file_key,
                              cfg_.host, e.what());
            } catch (...) {
                spdlog::error("S3Session CRITICAL: Unknown non-standard exception caught!");
            }
        }
    }
}
