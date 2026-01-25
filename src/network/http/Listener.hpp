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
  static std::expected<std::shared_ptr<Listener>, ErrorInfo> Create(
      asio::io_context& main_ioc, std::shared_ptr<IoContextPool> pool,
      const tcp::endpoint& endpoint, std::shared_ptr<http::Router> router);
  // Start accepting incoming connections
  void run();

 private:
  // Accept a new connection
  asio::awaitable<std::expected<void,ErrorInfo>> do_accept();

  Listener(asio::io_context& main_ioc, std::shared_ptr<IoContextPool> pool,
           tcp::acceptor&& acceptor, std::shared_ptr<http::Router> router);
  // Member variables

  tcp::acceptor acceptor_;
  boost::asio::io_context& main_ioc_;
  std::shared_ptr<IoContextPool> pool_;
  std::shared_ptr<http::Router> router_;
};
};  // namespace hermes::net
