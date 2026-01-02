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
#include "Types.hpp"

/**
 * @brief Immutable S3 config storage. Acts as a factory for S3Sessions.
    Thread-safe (read-only).
 */
class S3Client : std::enable_shared_from_this<S3Client> {
   public:
    explicit S3Client(S3Config cfg = {});

    std::shared_ptr<S3Session> CreateSession(asio::io_context& ioc);

   private:
    S3Config cfg_;
};
