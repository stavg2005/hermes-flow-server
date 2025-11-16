// S3Session.cpp (Implementation file)

#if defined(__linux__)
#define BOOST_ASIO_HAS_IO_URING 1
#endif

#include "S3Session.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/stream_file.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/write.hpp>
#include <boost/system/system_error.hpp>
#include <ctime>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

#include "awssigv4.h"
#include "boost/asio/redirect_error.hpp"
#include "spdlog/spdlog.h"

namespace beast = boost::beast;
namespace net = boost::asio;
using tcp = net::ip::tcp;
namespace http = beast::http;
using namespace hcm;

// --- build_download_request ---
http::request<http::empty_body> S3Session::build_download_request(
    const std::string& file_key) const {
    std::time_t now = std::time(nullptr);
    Signature sig(cfg_.service, cfg_.host, cfg_.region, cfg_.secret_key, cfg_.access_key, now);

    std::string canonical_uri = "/" + cfg_.bucket + "/" + file_key;
    const std::string query_string;
    const std::string payload;
    std::string payload_hash;

    std::string auth_header = sig.getAuthorization("GET", canonical_uri, query_string, payload,
                                                   payload_hash, SINGLE_CHUNK);

    http::request<http::empty_body> req{http::verb::get, canonical_uri, 11};
    req.set(http::field::host, cfg_.host);
    req.set(http::field::content_type, "application/octet-stream");
    req.set(http::field::authorization, auth_header);
    req.set("x-amz-date", sig.getdate());
    req.set("x-amz-content-sha256", payload_hash);

    return req;
}

// --- do_download_file (MEMORY-EFFICIENT: uses raw socket for body) ---
net::awaitable<void> S3Session::do_download_file(std::string file_key) {
    spdlog::info(">>>> [MEMORY-EFFICIENT S3 DOWNLOAD v5 - RAW SOCKET] STARTING {} <<<<", file_key);

    beast::error_code ec;
    size_t expected_size = 0;
    std::filesystem::path local_path;

    try {
        // 1. Resolve + connect using raw socket
        auto results = co_await resolver_.async_resolve(cfg_.host, cfg_.port, net::use_awaitable);
        net::connect(stream_.socket(), results, ec);
        if (ec) {
            spdlog::error("Connect failed: {}", ec.message());
            co_return;
        }

        // 2. Build and send signed request (still uses Beast stream for HTTP write)
        auto req = build_download_request(file_key);
        co_await http::async_write(stream_, req, net::use_awaitable);

        // 3. Read only headers — requires dynamic_buffer

        {
            beast::flat_buffer header_buffer;
            http::response_parser<http::empty_body> parser;
            parser.body_limit(2ULL * 1024 * 1024 * 1024);  // 2 GiB cap — adjust to your needs

            co_await http::async_read_header(stream_, header_buffer, parser, net::use_awaitable);

            const auto& response = parser.release();
            if (response.result() != http::status::ok) {
                spdlog::error("S3 returned {} for {}", response.result_int(), file_key);
                co_return;
            }

            if (auto it = response.find(http::field::content_length); it != response.end()) {
                expected_size = std::stoull(std::string(it->value()));
                spdlog::info("Downloading {} ({:.2f} MB)", file_key,
                             expected_size / (1024.0 * 1024.0));
            }

            // Free header_buffer capacity immediately:
            header_buffer = boost::beast::flat_buffer();
        }

        // 4. Prepare local file
        const std::filesystem::path local_dir = "downloads";
        const auto filename = std::filesystem::path(file_key).filename();
        local_path = local_dir / filename;
        std::error_code dir_ec;
        std::filesystem::create_directories(local_dir, dir_ec);

        net::stream_file file(ioc_);
        file.open(
            local_path.string(),
            net::stream_file::write_only | net::stream_file::create | net::stream_file::truncate,
            ec);
        if (ec) {
            spdlog::error("Cannot open local file {}: {}", local_path.string(), ec.message());
            co_return;
        }

        // 5. FIXED-SIZE BUFFER FOR BODY STREAMING
        static constexpr size_t CHUNK_SIZE = 524288;  // 512 KB
        std::array<std::uint8_t, CHUNK_SIZE> body_buffer;

        size_t total_written = 0;
        size_t last_logged_mb = 0;

        // 6. Stream body directly to disk — USING RAW SOCKET
        while (true) {
            beast::error_code read_ec;
            size_t bytes_read = co_await stream_.socket().async_read_some(
                net::buffer(body_buffer), net::redirect_error(read_ec));

            if (read_ec == net::error::eof || bytes_read == 0) {
                break;
            }
            if (read_ec) {
                spdlog::error("Socket read error: {}", read_ec.message());
                co_return;
            }

            net::const_buffer to_write(body_buffer.data(), bytes_read);
            while (to_write.size() > 0) {
                size_t written = co_await file.async_write_some(to_write, net::use_awaitable);
                if (written == 0) {
                    spdlog::error("Disk write returned 0 bytes");
                    co_return;
                }
                to_write += written;
                total_written += written;

                size_t current_mb = total_written / (1024 * 1024);
                if (current_mb >= last_logged_mb + 10) {
                    spdlog::info("... {} MB downloaded", current_mb);
                    last_logged_mb = current_mb;
                }
            }

            if (expected_size > 0 && total_written >= expected_size) {
                break;
            }
        }

        // 7. Cleanup
        file.close(ec);
        spdlog::info("SUCCESS: Downloaded {} — {} bytes ({:.2f} MB)", file_key, total_written,
                     total_written / (1024.0 * 1024.0));

        // Graceful shutdown
        stream_.socket().shutdown(tcp::socket::shutdown_both, ec);
    } catch (const boost::system::system_error& e) {
        spdlog::error("Download failed for '{}': {} (code: {})", file_key, e.what(),
                      e.code().value());
        if (!local_path.empty() && std::filesystem::exists(local_path)) {
            std::filesystem::remove(local_path);
        }
    } catch (const std::exception& e) {
        spdlog::error("Unexpected exception for '{}': {}", file_key, e.what());
    }

    stream_.socket().close(ec);
    co_return;
}

// --- RequestFile ---
void S3Session::RequestFile(std::string file) {
    net::co_spawn(
        ioc_,
        [self = shared_from_this(), file_key = std::move(file)]() -> net::awaitable<void> {
            co_return co_await self->do_download_file(file_key);
        },
        [self = shared_from_this(), key = file](std::exception_ptr p) {
            if (p) {
                try {
                    std::rethrow_exception(p);
                } catch (const boost::system::system_error& e) {
                    spdlog::error("S3Session failed for file '{}' (host: {}) with system error: {}",
                                  key, self->cfg_.host, e.what());
                } catch (const std::exception& e) {
                    spdlog::error("S3Session failed for file '{}' (host: {}) with exception: {}",
                                  key, self->cfg_.host, e.what());
                }
            }
        });
}
