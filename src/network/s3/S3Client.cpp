#include "S3Client.hpp"
#include <iostream>
#include "boost/asio/io_context.hpp"
#include "types.hpp"
S3Client::S3Client(S3Config cfg) : cfg_(std::move(cfg)) {
  spdlog::info("created S3Client");
}

  std::shared_ptr<S3Session> S3Client::CreateSession(
     asio::io_context &ioc) {
        spdlog::trace("creating session");
    return std::make_shared<S3Session>(ioc, cfg_);
  }
