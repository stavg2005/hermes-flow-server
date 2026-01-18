#pragma once

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <memory>

#include "IoContextPool.hpp"
#include "Router.hpp"
#include "Types.hpp"
#include "boost/asio/io_context.hpp"

class ServerContext;
using namespace ::hermes::net::http;
namespace hermes::net {
/**
 * @brief The TCP Connection Acceptor.
 **/
class Listener : public std::enable_shared_from_this<Listener> {
 public:
  static std::expected<std::shared_ptr<Listener>, ErrorInfo> Create(
      asio::io_context& main_ioc, std::shared_ptr<IoContextPool> pool,
      const tcp::endpoint& endpoint, std::shared_ptr<Router> router);
  // Start accepting incoming connections
  void run();

 private:
  // Accept a new connection
  asio::awaitable<void> do_accept();

  Listener(asio::io_context& main_ioc, std::shared_ptr<IoContextPool> pool,
           tcp::acceptor&& acceptor, std::shared_ptr<Router> router);
  // Member variables

  tcp::acceptor acceptor_;
  boost::asio::io_context& main_ioc_;
  std::shared_ptr<IoContextPool> pool_;
  std::shared_ptr<Router> router_;
};
};  // namespace hermes::net
