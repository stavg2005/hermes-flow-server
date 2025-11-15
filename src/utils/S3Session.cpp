
#define BOOST_ASIO_HAS_IO_URING 1

#include "S3Session.hpp"

#include <boost/asio/stream_file.hpp>

#include "awssigv4.h"
#include "boost/asio/as_tuple.hpp"
#include "boost/asio/awaitable.hpp"
#include "boost/asio/use_awaitable.hpp"
#include "boost/beast/http.hpp"
#include "boost/beast/http/write.hpp"
#include "spdlog/spdlog.h"

namespace beast = boost::beast;
namespace net = boost::asio;
using tcp = net::ip::tcp;
namespace http = beast::http;
using namespace hcm;
void S3Session::RequestFile(std::string file) {
  net::co_spawn(
      ioc_, [this, self = shared_from_this(), file]() -> net::awaitable<void> {
        auto [ec, results] = co_await resolver_.async_resolve(
            cfg_.host, cfg_.port, net::as_tuple(net::use_awaitable));
        if (ec) {
          spdlog::error("In S3Session: An exception has occured {}", ec.what());
          co_return;
        }

        auto [ec2, socket] = co_await stream_.async_connect(
            results, net::as_tuple(net::use_awaitable));
        if (ec2) {
          spdlog::error("In S3Session: An exception has occured {}",
                        ec2.what());
          co_return;
        }

        time_t now = std::time(nullptr);

        // -------------------- SIGV4 --------------------
        Signature sig(cfg_.service, cfg_.host, cfg_.region, cfg_.secret_key,
                      cfg_.access_key, now);

        std::string canonical_uri = "/" + cfg_.bucket + "/" + file;
        std::string query_string;
        std::string payload;
        std::string payload_hash;

        std::string auth_header =
            sig.getAuthorization("GET", canonical_uri, query_string, payload,
                                 payload_hash, SINGLE_CHUNK);

        // -------------------- HTTP REQUEST --------------------
        http::request<http::empty_body> req{http::verb::get, canonical_uri, 11};
        req.set(http::field::host, cfg_.host);
        req.set(http::field::content_type, "application/octet-stream");
        req.set(http::field::authorization, auth_header);
        req.set("x-amz-date", sig.getdate());
        req.set("x-amz-content-sha256", payload_hash);

        auto [ec3, bytes_wrote] = co_await http::async_write(
            stream_, req, net::as_tuple(net::use_awaitable));
        if (ec3) {
          spdlog::error("In S3Session: An exception has occured {}",
                        ec2.what());
          co_return;
        }

        http::response<http::dynamic_body> res;

        auto [ec4, bytes_read] = co_await http::async_read(
            stream_, buffer_, res, net::as_tuple(net::use_awaitable));
        if (ec4) {
          spdlog::error("In S3Session: An exception has occured {}",
                        ec2.what());
          co_return;
        }

        if (res.result() != http::status::ok) {
          co_return;
        }

        boost::asio::stream_file disk_file(ioc_, file,
                                           net::stream_file::write_only);

        auto [ec5, bytes_written] = co_await net::async_write(
            disk_file, res.body().data(), net::as_tuple(net::use_awaitable));
        if (ec5) {
          spdlog::error("In S3Session: An exception has occured {}",
                        ec2.what());
          co_return;
        }
        spdlog::info("file {} download succsefully", file);

        beast::error_code ec6;
        stream_.socket().shutdown(tcp::socket::shutdown_both, ec6);
        if (ec && ec != beast::errc::not_connected)
          throw beast::system_error{ec};
      });
}
