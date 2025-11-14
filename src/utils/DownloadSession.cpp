#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <fstream>
#include <string>
#include <vector>
#include <memory> // For enable_shared_from_this

// --- Includes for coroutines ---
#include "boost/asio/awaitable.hpp"
#include "boost/asio/co_spawn.hpp"
#include "boost/asio/detached.hpp"
#include "boost/asio/use_awaitable.hpp"
#include "boost/asio/as_tuple.hpp" // For as_tuple

// --- Includes for date/time fix ---
#include <chrono>
#include <sstream>
#include <iomanip>

#include "boost/beast/core/file_base.hpp"
#include "boost/beast/core/tcp_stream.hpp"
#include "spdlog/spdlog.h"

extern "C" {
#include "sigv4.h"
}

namespace beast = boost::beast;
namespace net = boost::asio;
namespace http = beast::http;
using tcp = net::ip::tcp;

class DownloadSession : public std::enable_shared_from_this<DownloadSession> {
public:
    DownloadSession(net::io_context& ioc, std::string host, std::string bucket,
                    std::string object, std::string accessKey,
                    std::string secretKey, std::string region = "us-east-1")
        : resolver_(ioc), // Use a strand for safety
          stream_(ioc),   // Use a strand for safety
          host_(std::move(host)),
          bucket_(std::move(bucket)),
          object_(std::move(object)),
          accessKey_(std::move(accessKey)),
          secretKey_(std::move(secretKey)),
          region_(std::move(region)) {}


    void run() {

        co_spawn(
            resolver_.get_executor(),
            [self = shared_from_this()]() -> net::awaitable<void> {
                try {
                    spdlog::info("Starting download for: s3://{}/{}", self->bucket_, self->object_);
                    // Await the entire download process
                    co_await self->do_download();

                    spdlog::info("Successfully downloaded: s3://{}/{}", self->bucket_, self->object_);

                } catch (const boost::system::system_error& e) {
                    spdlog::error("Download failed for s3://{}/{}: {} (Code: {})",
                                  self->bucket_, self->object_, e.what(), e.code().message());
                } catch (const std::exception& e) {
                    spdlog::error("Download failed for s3://{}/{}: {}",
                                  self->bucket_, self->object_, e.what());
                }
            },
            net::detached // We don't wait for the result here
        );
    }

private:

    net::awaitable<void> do_download() {
        boost::system::error_code ec;

        // --- 1. Resolve ---

        auto [ec_resolve, results] = co_await resolver_.async_resolve(host_, "http", net::as_tuple(net::use_awaitable));
        if (ec_resolve) {
            throw boost::system::system_error(ec_resolve, "Resolve");
        }
        // --- 2. Connect ---
        auto& socket = stream_.socket();
        auto [ec_connect, _] = co_await net::async_connect(socket, results, net::as_tuple(net::use_awaitable));
        if (ec_connect) {
            throw boost::system::system_error(ec_connect, "Connect");
        }

        // --- 3. Prepare SigV4 Request ---
        // (This part is synchronous)
        // 1. Canonicalize URI
        char canonicalUri[1024];
        size_t canonicalLen = sizeof(canonicalUri);
        SigV4_EncodeURI(object_.c_str(), object_.size(), canonicalUri,
                        &canonicalLen, false, false);

        // 2. Prepare HTTP parameters
        SigV4HttpParameters_t httpParams = {};
        httpParams.pHttpMethod = "GET";
        httpParams.httpMethodLen = 3;
        httpParams.pPath = canonicalUri;
        httpParams.pathLen = canonicalLen;
        httpParams.pQuery = nullptr;
        httpParams.queryLen = 0;
        httpParams.pHeaders = nullptr;
        httpParams.headersLen = 0;
        httpParams.pPayload = nullptr; // Must be set, even if null
        httpParams.payloadLen = 0; // Must be set

        // 3. Prepare credentials
        SigV4Credentials_t creds = {};
        creds.pAccessKeyId = accessKey_.c_str();
        creds.accessKeyIdLen = accessKey_.size();
        creds.pSecretAccessKey = secretKey_.c_str();
        creds.secretAccessKeyLen = secretKey_.size();


        // --- CRITICAL FIX: Generate current date/time ---
        // S3 requires the current time in UTC. A hardcoded date will fail.
        char dateISO[SIGV4_ISO_STRING_LEN] = {};
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        std::ostringstream ss;
        ss << std::put_time(std::gmtime(&t), "%Y%m%dT%H%M%SZ");
        std::string dateISO_str = ss.str();
        strncpy(dateISO, dateISO_str.c_str(), sizeof(dateISO) - 1);
        // -----------------------------------------------

        SigV4Parameters_t sigParams = {};
        sigParams.pCredentials = &creds;
        sigParams.pDateIso8601 = dateISO;
        sigParams.pRegion = region_.c_str();
        sigParams.regionLen = region_.size();
        sigParams.pService = "s3";
        sigParams.serviceLen = 2; // "s3"
        sigParams.pCryptoInterface = nullptr;
        sigParams.pHttpParameters = &httpParams;

        char authBuf[2048];
        size_t authLen = sizeof(authBuf);
        char* pSignature = nullptr;
        size_t sigLen = 0;

        SigV4Status_t status = SigV4_GenerateHTTPAuthorization(
            &sigParams, authBuf, &authLen, &pSignature, &sigLen);
        if (status != SigV4Success) {
            throw std::runtime_error("SigV4_GenerateHTTPAuthorization failed");
        }

        // 4. Build Beast request
        req_.version(11);
        req_.method(http::verb::get);
        req_.target("/" + bucket_ + "/" + object_); // Target must include bucket
        req_.set(http::field::host, host_);
        req_.set("Authorization", std::string(authBuf, authLen));
        req_.set("x-amz-date", dateISO);
        // --- CRITICAL FIX: Typo "sha26" -> "sha256" ---
        req_.set("x-amz-content-sha256", "UNSIGNED-PAYLOAD");
        req_.prepare_payload(); // Good practice

        // --- 4. Write Request ---
        auto [ec_write, bytes_written] = co_await http::async_write(stream_, req_, net::as_tuple(net::use_awaitable));
        if (ec_write) {
            throw boost::system::system_error(ec_write, "Write");
        }

        // --- 5. Read Response ---
        // Clear the response object and buffer for the new response
        res_ = {};
        buffer_.clear();

        // Open the file to write to.
        // Using object_ as filename, but "downloaded_file.bin" is also fine.
        std::string output_filename = object_;
        res_.body().open(output_filename.c_str(),beast::file_mode::read,ec);
        if (ec) {
            throw boost::system::system_error(ec, "Open output file");
        }

        auto [ec_read, bytes_read] = co_await http::async_read(stream_, buffer_, res_, net::as_tuple(net::use_awaitable));

        // 'end_of_stream' is a normal and expected way for a read to end
        if (ec_read && ec_read != http::error::end_of_stream) {
            throw boost::system::system_error(ec_read, "Read");
        }

        // Close the file body
        res_.body().close();

        // --- 6. Shutdown Socket (Graceful) ---
        stream_.socket().shutdown(tcp::socket::shutdown_both);
        // We don't check 'ec' here, as we're done anyway.

        co_return;
    }

    // Member variables
    tcp::resolver resolver_;
    beast::tcp_stream stream_;
    beast::flat_buffer buffer_;
    http::request<http::empty_body> req_;
    http::response<http::file_body> res_;

    std::string host_;
    std::string bucket_;
    std::string object_;
    std::string accessKey_;
    std::string secretKey_;
    std::string region_;
};
