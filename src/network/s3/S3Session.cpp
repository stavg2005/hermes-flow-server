#include "S3Session.hpp"

#include "Config.hpp"
#include "spdlog/spdlog.h"
#include "Types.hpp"

using namespace hermes::config;
namespace hermes::net::s3 {


S3Session::S3Session(boost::asio::io_context& ioc, S3Config cfg)
    : ioc_(ioc), cfg_(std::move(cfg)) {}


std::expected<std::shared_ptr<S3Session>,ErrorInfo>
S3Session::create(boost::asio::io_context& ioc,
                  const S3Config& manual_cfg) {
  S3Config final_config = manual_cfg;


  if (final_config.access_key.empty()) {
    spdlog::debug("[S3Session] No config provided, loading from config.toml");

    
    auto config_result =load_config("../config.toml");

    if (!config_result) {
      spdlog::error("S3Session failed to load config: {}",
                    config_result.error().message);
      return std::unexpected(config_result.error());
    }

    final_config = std::move(config_result.value().s3);
  }

  return std::shared_ptr<S3Session>(
      new S3Session(ioc, std::move(final_config)));
}


}  // namespace hermes::net::s3
