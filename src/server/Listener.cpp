
#include "Listener.hpp"
#include "Router.hpp"
#include "boost/asio.hpp"
#include "http_session.hpp"
#include <boost/beast.hpp>
#include <iostream>

namespace beast = boost::beast;
namespace net = boost::asio;
// Report a failure
static void fail(beast::error_code ec, const char *what) {
  if (ec != beast::errc::not_connected && ec != net::error::eof &&
      ec != net::error::connection_reset) {
    std::cerr << what << ": " << ec.message() << "\n";
  }
}

listener::listener(net::io_context &ioc, tcp::endpoint endpoint,
                   std::shared_ptr<Router> &router)
    : acceptor_(ioc), io_(ioc), router_(router) {

  beast::error_code ec;

  // Open the acceptor
  auto ec2 = acceptor_.open(endpoint.protocol(), ec);
  if (ec2) {
    fail(ec, "open");
    return;
  }

  // Allow address reuse
  ec2 = acceptor_.set_option(net::socket_base::reuse_address(true), ec);
  if (ec2) {
    fail(ec, "set_option");
    return;
  }

  // Bind to the server address
  ec2 = acceptor_.bind(endpoint, ec);
  if (ec2) {
    fail(ec, "bind");
    return;
  }

  // Start listening for connections
  ec2 = acceptor_.listen(net::socket_base::max_listen_connections, ec);
  if (ec2) {
    fail(ec, "listen");
    return;
  }

  std::cout << " Listener successfully bound to " << endpoint.address() << ":"
            << endpoint.port() << std::endl;
}

void listener::run() {
  std::cout << " Starting to accept connections..." << std::endl;
  do_accept();
}

void listener::do_accept() {

  acceptor_.async_accept([this, self = shared_from_this()](beast::error_code ec,
                                                           tcp::socket socket) {
    if (ec) {
      fail(ec, "accept");

      // Don't return here - continue accepting connections
      // even if one fails
    } else {
      std::cout << "new connection accepted" << std::endl;
      std::make_shared<server::core::HttpSession>(std::move(socket), io_,
                                                  router_)
          ->run();
    }

    do_accept();
  });
};
