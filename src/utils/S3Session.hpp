
#pragma once

#include <boost/asio.hpp>
#include <boost/asio/random_access_file.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/beast.hpp>
#include <boost/smart_ptr/enable_shared_from_this.hpp>
#include <memory>
#include <string>
#include <string_view>


namespace beast = boost::beast;
namespace net = boost::asio;
using tcp = net::ip::tcp;
namespace http = beast::http;

// compile-time defaults
inline constexpr std::string_view DEFAULT_ACCESS_KEY = "minioadmin";
inline constexpr std::string_view DEFAULT_SECRET_KEY = "minioadmin123";
inline constexpr std::string_view DEFAULT_REGION = "us-east-1";
inline constexpr std::string_view DEFAULT_HOST = "localhost";
inline constexpr std::string_view DEFAULT_PORT = "9001";
inline constexpr std::string_view DEFAULT_SERVICE = "s3";
inline constexpr std::string_view DEFAULT_BUCKET = "audio-files";

struct S3Config {
  std::string access_key = std::string(DEFAULT_ACCESS_KEY);
  std::string secret_key = std::string(DEFAULT_SECRET_KEY);
  std::string region = std::string(DEFAULT_REGION);
  std::string host = std::string(DEFAULT_HOST);
  std::string port = std::string(DEFAULT_PORT);
  std::string service = std::string(DEFAULT_SERVICE);
  std::string bucket = std::string(DEFAULT_BUCKET);
};



class S3Session : public std::enable_shared_from_this<S3Session> {
 public:
  S3Session(net::io_context& ioc, const S3Config& cfg)
      : ioc_(ioc), cfg_(cfg), resolver_(ioc), stream_(ioc) {}

  void RequestFile(std::string file);

 private:
  net::io_context& ioc_;
  S3Config cfg_;
  tcp::resolver resolver_;
  beast::tcp_stream stream_;
  beast::flat_buffer buffer_;
};
