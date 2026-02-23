#include "S3Session.hpp"

#include "Config.hpp"
#include "Types.hpp"
#include "spdlog/spdlog.h"

using namespace hermes::config;
namespace hermes::net::s3 {

S3Session::S3Session(boost::asio::io_context& ioc, S3Config cfg)
    : ioc_(ioc), cfg_(std::move(cfg)) {}

std::expected<S3Session*, ErrorInfo> S3Session::create(
    boost::asio::io_context& ioc, const S3Config& manual_cfg) {
  S3Config final_config = manual_cfg;

  if (manual_cfg.access_key.empty()) {
    spdlog::error("S3Session created without valid credentials.");
    return std::unexpected(
        ErrorInfo::From(AppError::ConfigError, "Missing S3 Configuration"));
  }
  return new S3Session(ioc, std::move(final_config));
}

}  // namespace hermes::net::s3
