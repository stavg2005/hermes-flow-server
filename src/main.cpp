#include "Router.hpp"
#include "io_context_pool.hpp" // <--- 1. Include your new header
#include "server//ServerContext.hpp"
#include "server/ActiveSessions.hpp"
#include "server/Listener.hpp"

#include <boost/asio.hpp>
#include <boost/beast/http.hpp>
#include <chrono>
#include <csignal>
#include <exception>
#include <iostream>
#include <memory>
#include <thread> // <--- For hardware_concurrency

namespace beast = boost::beast;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

// fail() function is unchanged...

int main(int argc, char *argv[]) {
  // Arg check is unchanged...

  try {
    auto const address = net::ip::make_address(argv[1]);
    auto const port = static_cast<unsigned short>(std::atoi(argv[2]));

    std::cout << " Hermes flow Server starting...\n";

    // --- 2. Create the pool instead of the single ioc ---
    //    (We make it a shared_ptr so it can be captured in the signal)
    auto const num_threads = std::max(1u, std::thread::hardware_concurrency());
    auto pool = std::make_shared<io_context_pool>(num_threads);

    std::cout << " Listening on " << address << ":" << port << " with "
              << num_threads << " I/O threads.\n";

    // --- 3. Get ONE io_context for "main" tasks ---
    //    We need one context to run the listener's acceptor and the signal set.
    auto &main_ioc = pool->get_io_context();

    auto sessions = ActiveSessions::create();
    auto router = std::make_shared<Router>(sessions);

    std::make_shared<listener>(main_ioc, *pool, tcp::endpoint{address, port},
                               router)
        ->run();

    //
    net::signal_set signals(main_ioc, SIGINT, SIGTERM);
    signals.async_wait([pool](beast::error_code const &, int) {
      std::cout << "\n Shutting down server...\n";
      pool->stop();
    });

    //    This blocks until all threads are stopped.
    pool->run();

    std::cout << "Server shutdown complete.\n";

  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}