#pragma once

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <expected>
#include <memory>

#include "IoContextPool.hpp"
#include "Router.hpp"
#include "Types.hpp"
#include "boost/asio/io_context.hpp"

class ServerContext;

namespace hermes::net {
/**
 * @brief The TCP Connection Acceptor.
 **/
class Listener : public std::enable_shared_from_this<Listener> {
 public:
  static std::expected<std::shared_ptr<Listener>, config::ErrorInfo> create(
      boost::asio::io_context& main_ioc, infra::IoContextPool& pool,
      const boost::asio::ip::tcp::endpoint& endpoint, std::shared_ptr<hermes::net::http::Router> router);
  // Start accepting incoming connections
  void run();

 private:
  // Accept a new connection
  boost::asio::awaitable<std::expected<void, config::ErrorInfo>> do_accept();

  Listener(boost::asio::io_context& main_ioc, infra::IoContextPool& pool,
           boost::asio::ip::tcp::acceptor&& acceptor, std::shared_ptr<http::Router> router);
  // Member variables

  boost::asio::ip::tcp::acceptor acceptor_;
  boost::asio::io_context& main_ioc_;
  infra::IoContextPool& pool_;
  std::shared_ptr<http::Router> router_;
};
};  // namespace hermes::net
