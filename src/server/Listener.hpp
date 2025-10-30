#pragma once

#include "Router.hpp"
#include "boost/asio/io_context.hpp"
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <memory>

namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

class ServerContext;
// Accepts incoming connections and launches sessions
class listener : public std::enable_shared_from_this<listener> {
public:
  // Constructor takes io_context and endpoint to bind to
  listener(net::io_context &ioc, tcp::endpoint endpoint,
           std::shared_ptr<Router> &router);

  // Start accepting incoming connections
  void run();

private:
  // Accept a new connection
  void do_accept();

  // Member variables

  tcp::acceptor acceptor_;
  boost::asio::io_context &io_;
  std::shared_ptr<Router> router_;
};