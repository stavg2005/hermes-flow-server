#pragma once

#include <memory>  // For std::enable_shared_from_this
#include <string>
#include <string_view>

// --- Boost.Asio Includes ---
#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>  // For tcp::resolver

// --- Boost.Beast Includes ---
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/message.hpp>  // For http::request

namespace beast = boost::beast;
namespace net = boost::asio;
using tcp = net::ip::tcp;
namespace http = beast::http;

// --- S3 Configuration ---
static constexpr std::size_t STREAM_CHUNK_SIZE = 128 * 1024;
// compile-time defaults (This was already clean)
inline constexpr std::string_view DEFAULT_ACCESS_KEY = "minioadmin";
inline constexpr std::string_view DEFAULT_SECRET_KEY = "minioadmin123";
inline constexpr std::string_view DEFAULT_REGION = "us-east-1";
inline constexpr std::string_view DEFAULT_HOST = "localhost";
inline constexpr std::string_view DEFAULT_PORT = "9000";
inline constexpr std::string_view DEFAULT_SERVICE = "s3";
inline constexpr std::string_view DEFAULT_BUCKET = "audio-files";

inline std::string get_env_or_default(const char* name, std::string_view default_val) {
    const char* val = std::getenv(name);
    return (val && val[0]) ? val : std::string(default_val);
}

// ... in S3Session.hpp ...
struct S3Config {
    std::string access_key{DEFAULT_ACCESS_KEY};
    std::string secret_key{DEFAULT_SECRET_KEY};
    std::string region{DEFAULT_REGION};

    // This is the important part
    std::string host{get_env_or_default("S3_HOST", DEFAULT_HOST)};
    std::string port{get_env_or_default("S3_PORT", DEFAULT_PORT)};

    std::string service{DEFAULT_SERVICE};
    std::string bucket{DEFAULT_BUCKET};
};

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
    explicit S3Session(net::io_context& ioc, const S3Config& cfg = {})
        : ioc_(ioc),
          cfg_(cfg),
          resolver_(ioc),
          stream_(ioc),
          buffer_(STREAM_CHUNK_SIZE)  // Explicitly default-initialize buffer
    {
        buffer_.max_size(STREAM_CHUNK_SIZE * 4);
    }

    /**
     * @brief Asynchronously requests a file download from S3.
     *
     * This function "fires and forgets" the download coroutine.
     *
     * @param file_key The object key (path) of the file in the S3 bucket.
     */
    void RequestFile(std::string file_key);

   private:
    /**
     * @brief The main coroutine implementing the download logic.
     */
    net::awaitable<void> do_download_file(std::string file_key);

    /**
     * @brief Creates and signs the S3 GET request.
     * @param file_key The object key to request.
     * @return A signed http::request.
     */
    http::request<http::empty_body> build_download_request(
        const std::string& file_key) const;  // Marked const

    // --- Member Variables ---
    net::io_context& ioc_;  // Must be declared before members that use it
    S3Config cfg_;
    tcp::resolver resolver_;
    beast::tcp_stream stream_;
    beast::flat_buffer buffer_{STREAM_CHUNK_SIZE};
};
