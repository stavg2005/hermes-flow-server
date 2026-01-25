#pragma once

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <expected>
#include <memory>
#include <string>

// Core Dependencies
#include "Concepts.hpp"
#include "Config.hpp"
#include "HttpStreamer.hpp"  // The Generic Client we built earlier
#include "S3RequestFactory.hpp"
#include "Types.hpp"
#include "spdlog/spdlog.h"
namespace hermes::net::s3 {

/**
 * @brief Orchestrator for S3 Operations.
 * * Responsibilities:
 * 1. Load/Manage S3 Configuration.
 * 2. Generate Signed S3 Requests.
 * 3. Delegate execution to HttpStreamer.
 * * Note: This class is agnostic to WHERE the data goes (File, Memory, Pipe).
 */
class S3Session : public std::enable_shared_from_this<S3Session> {
 public:
  // Factory Method (Loads config if not provided)
  static std::expected<std::shared_ptr<S3Session>, hermes::config::ErrorInfo>
  Create(boost::asio::io_context& ioc,
         const hermes::config::S3Config& manual_cfg = {});

  /**
   * @brief Downloads a file from S3 to a generic Sink.
   * * @tparam Sink A type satisfying the AsyncWriteStream concept.
   * @param file_key The S3 object key.
   * @param destination Reference to the sink where data will be written.
   */
  template <config::AsyncWriteStream Sink>
  boost::asio::awaitable<std::expected<void, hermes::config::ErrorInfo>>
  RequestFile(const std::string& file_key, Sink& destination) {
    spdlog::debug("[S3Session] Preparing request for key: {}", file_key);

    auto req = S3RequestFactory::create_signed_get_request(
        cfg_, boost::beast::http::verb::get, file_key);

    http::HttpStreamer streamer(ioc_);

    auto result = co_await streamer.Execute(cfg_.host, cfg_.port,
                                            std::move(req), destination);

    if (!result) {
      spdlog::error("[S3Session] Download failed for {}: {}", file_key,
                    result.error().message);
      co_return std::unexpected(result.error());
    }

    co_return std::expected<void, config::ErrorInfo>();
  };

 private:
  // Constructor is private to enforce Factory pattern
  explicit S3Session(boost::asio::io_context& ioc,
                     hermes::config::S3Config cfg);

  boost::asio::io_context& ioc_;
  hermes::config::S3Config cfg_;
};

}  // namespace hermes::net::s3
