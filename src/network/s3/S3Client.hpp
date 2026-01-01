// s3_client.hpp
#pragma once

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/beast.hpp>
#include <boost/smart_ptr/enable_shared_from_this.hpp>
#include <memory>

#include "S3Session.hpp"
#include "spdlog/spdlog.h"


#include "types.hpp"
/**
 * @brief Factory for S3 Sessions.
 * * @details
 * **Role:**
 * Stores the immutable S3 configuration (Credentials, Bucket, Region) loaded
 * at startup. It acts as a factory to spawn lightweight, short-lived
 * `S3Session` objects for individual file downloads.
 * * **Thread Safety:**
 * Since configuration is read-only after startup, this class is thread-safe.
 */
class S3Client :std::enable_shared_from_this<S3Client>{
 public:
  explicit S3Client(S3Config cfg = {});

  std::shared_ptr<S3Session> CreateSession(asio::io_context& ioc);

 private:
  S3Config cfg_;
};
