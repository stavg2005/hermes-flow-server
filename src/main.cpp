#include <boost/asio.hpp>
#include <boost/beast/http.hpp>
#include <csignal>
#include <exception>
#include <iostream>
#include <memory>
#include <thread>

#include "Router.hpp"
#include "io_context_pool.hpp"
#include "server/ActiveSessions.hpp"
#include "server/Listener.hpp"

// --- LOGGER INCLUDES ---
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/spdlog.h"

namespace beast = boost::beast;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

void setup_logging() {
  // 1. Create a list of sinks
  std::vector<spdlog::sink_ptr> sinks;

  // 2. Create the Console Sink
  auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  console_sink->set_level(spdlog::level::info);  // CONSOLE_LEVEL
  sinks.push_back(console_sink);

  // 3. Create a Rotating File Sink
  auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
      "logs/server.log", 1024 * 1024 * 5, 3);
  file_sink->set_level(spdlog::level::trace);  // FILE_LEVEL
  sinks.push_back(file_sink);

  // 4. Create and register the logger
  auto logger =
      std::make_shared<spdlog::logger>("server", sinks.begin(), sinks.end());
  spdlog::register_logger(logger);
  spdlog::set_default_logger(logger);

  // 5. Set global levels and format
  spdlog::set_level(spdlog::level::trace);
  spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [thread %t] %v");
}

int main(int argc, char *argv[]) {
  // --- 1. Set up logging FIRST ---
  setup_logging();

  try {
    // --- 2. Argument Validation ---
    if (argc != 3) {
      spdlog::critical("Usage: hermes_server <address> <port>");
      spdlog::critical("Example: hermes_server 0.0.0.0 8080");
      return EXIT_FAILURE;
    }

    // --- 3. Configuration Parsing ---
    auto const address = net::ip::make_address(argv[1]);
    auto const port = static_cast<unsigned short>(std::atoi(argv[2]));
    auto const num_threads = std::max(1u, std::thread::hardware_concurrency());

    // --- LINTER FIX: WRAP STRINGS IN fmt::runtime() ---
    spdlog::info("Listening on {}:{} with {} I/O threads.", address.to_string(),
                 port, num_threads);
    spdlog::debug("io_context_pool created with {} contexts.", num_threads);
    // --- END LINTER FIX ---

    // --- 4. I/O Thread Pool ---
    auto pool = std::make_shared<io_context_pool>(num_threads);
    auto &main_ioc = pool->get_io_context();

    // --- 5. Application Core Components ---
    auto sessions = ActiveSessions::create();
    auto router = std::make_shared<Router>(sessions);
    spdlog::debug("Router and ActiveSessions created.");

    // --- 6. Start Network Listener ---
    std::make_shared<listener>(main_ioc, *pool, tcp::endpoint{address, port},
                               router)
        ->run();

    // --- 7. Setup Graceful Shutdown ---
    net::signal_set signals(main_ioc, SIGINT, SIGTERM);
    signals.async_wait([pool](beast::error_code const &, int) {
      spdlog::info("\nShutting down server...");
      pool->stop();
    });
    spdlog::trace("Signal set waiting for SIGINT/SIGTERM.");

    // --- 8. Run the Server ---
    // This blocks the main thread until the pool is stopped.
    pool->run();

    // --- 9. Shutdown Complete ---
    spdlog::info("Server shutdown complete.");

  } catch (const std::exception &e) {
    spdlog::critical("Fatal Error: {}", e.what());
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
