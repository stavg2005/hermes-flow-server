

#include "Router.hpp"
#include "server//ServerContext.hpp"
#include "server/Listener.hpp"
#include <boost/asio.hpp>
#include <boost/beast/http.hpp>
#include <csignal>
#include <exception>
#include <iostream>
#include <memory>

namespace beast = boost::beast;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;
class listener;
class ServerContext;

static void fail(beast::error_code ec, const char *what) {
  if (ec != beast::errc::not_connected && ec != net::error::eof &&
      ec != net::error::connection_reset) {
    std::cerr << what << ": " << ec.message() << "\n";
  }
}

int main(int argc, char *argv[]) {
  if (argc != 3) {
    std::cerr << "Usage:  <address> <port>\n";
    return EXIT_FAILURE;
  }

  try {
    auto const address = net::ip::make_address(argv[1]);
    auto const port = static_cast<unsigned short>(std::atoi(argv[2]));

    std::cout << " Hermes flow Server starting...\n";
    std::cout << " Listening on " << address << ":" << port << "\n";

    net::io_context ioc;
    auto router = std::make_shared<Router>();

    // Pass database and router to listener
    std::make_shared<listener>(ioc, tcp::endpoint{address, port}, router)
        ->run();

    // Signal handling...
    net::signal_set signals(ioc, SIGINT, SIGTERM);
    signals.async_wait([&](beast::error_code const &, int) {
      std::cout << "\n Shutting down server...\n";
      ioc.stop();
    });

    ioc.run();
    std::cout << "Server shutdown complete.\n";

  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}