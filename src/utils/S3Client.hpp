// s3_client.hpp
#pragma once

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/beast.hpp>
#include <boost/smart_ptr/enable_shared_from_this.hpp>
#include <memory>

#include "S3Session.hpp"

namespace beast = boost::beast;
namespace net = boost::asio;
using tcp = net::ip::tcp;
namespace http = beast::http;

class S3Client {
 public:
  explicit S3Client(S3Config cfg = {}) : cfg_(std::move(cfg)) {}

  std::shared_ptr<S3Session> CreateSession(net::io_context& ioc) {
    return std::make_shared<S3Session>(ioc, cfg_);
  }

 private:
  S3Config cfg_;
};
