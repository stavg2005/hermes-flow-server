#include "Listener.hpp"
#include "Router.hpp"
#include "http_session.hpp" // Make sure this path is correct
#include "io_context_pool.hpp"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <iostream>

// --- Add these new includes for coroutines ---
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>
// ---

namespace beast = boost::beast;
namespace net = boost::asio;

// Report a failure
static void fail(beast::error_code ec, const char *what) {
  if (ec != beast::errc::not_connected && ec != net::error::eof &&
      ec != net::error::connection_reset) {
        
    std::cerr << what << ": " << ec.message() << "\n";
  }
}

// Constructor is unchanged
listener::listener(net::io_context &main_ioc, io_context_pool &pool,
                   tcp::endpoint endpoint,
                   const std::shared_ptr<Router> &router)
    : acceptor_(main_ioc), main_ioc_(main_ioc), pool_(pool), router_(router) {

  beast::error_code ec;

  // Open the acceptor
  auto ec2 = acceptor_.open(endpoint.protocol(), ec);
  if (ec2) {
    fail(ec, "open");
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

  net::co_spawn(
      acceptor_.get_executor(),
      [this, self = shared_from_this()]() { return do_accept(); },
      net::detached);
}

net::awaitable<void> listener::do_accept() {
  for (;;) {
    auto &pool_ioc = pool_.get_io_context();

    auto [ec, socket] = co_await acceptor_.async_accept(
        pool_ioc, net::as_tuple(net::use_awaitable));

    if (ec) {
      if (ec == net::error::operation_aborted) {
        break;
      }
      fail(ec, "accept");
      continue;
    }

    std::cout << "New connection accepted (on pool thread "
              << &socket.get_executor().context() << ")" << std::endl;

    // 4. Create the session.
    std::make_shared<server::core::HttpSession>(
        std::move(socket), // The socket is already on a pool ioc
        router_            // Pass the router
        )
        ->run();

  }
}
