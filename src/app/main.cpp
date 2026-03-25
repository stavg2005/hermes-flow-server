
#include <spdlog/async.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <boost/asio.hpp>
#include <boost/beast/http.hpp>
#include <csignal>
#include <exception>
#include <memory>
#include <vector>

#include "Config.hpp"
#include "NodeRegistry.hpp"
#include "Server.hpp"
#include "Types.hpp"

using namespace hermes::net;
using namespace hermes::audio;
static void SetupLogging() {
  std::vector<spdlog::sink_ptr> sinks;

  // A. Console Sink
  auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  console_sink->set_level(spdlog::level::debug);
  sinks.push_back(console_sink);

  // B. Rotating File Sink (Max 5MB, 3 files)
  constexpr size_t MAX_SIZE = 1024UZ * 1024 * 5;
  constexpr size_t MAX_FILES = 3;
  auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
      "logs/server.log", MAX_SIZE, MAX_FILES);
  file_sink->set_level(spdlog::level::trace);
  sinks.push_back(file_sink);

  // C. Register Logger
  auto logger =
      std::make_shared<spdlog::logger>("server", sinks.begin(), sinks.end());
  spdlog::register_logger(logger);
  spdlog::set_default_logger(logger);

  // D. Global Formatting
  spdlog::set_level(spdlog::level::debug);
  spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [thread %t] %v");
  spdlog::flush_on(spdlog::level::debug);
}

int main(int argc, char* argv[]) {
  // Safety net for unexpected runtime crashes (e.g. std::bad_alloc)
  try {
    SetupLogging();
    auto cfg_result = load_config("../config.toml");
    if (!cfg_result) {
      cfg_result = load_config("config.toml");
    }

    if (!cfg_result) {
      spdlog::critical("Failed to load configuration: {}",
                       cfg_result.error().message);
      return EXIT_FAILURE;
    }

    auto cfg = std::move(*cfg_result);

    hermes::audio::register_builtin_nodes();

    asio::io_context main_ioc;
    auto server_result = Server::create(main_ioc, cfg);

    if (!server_result) {
      spdlog::critical("Server Init Failed: {}", server_result.error().message);
      return EXIT_FAILURE;
    }

    auto server = std::move(*server_result);

    asio::signal_set signals(main_ioc, SIGINT, SIGTERM);
    signals.async_wait(
        [&server](const boost::system::error_code&, int signal_number) {
          spdlog::info("Stop signal ({}) received. Shutting down...",
                       signal_number);
          server->stop();
        });

    spdlog::info("Hermes Flow Server starting on {}:{}", cfg.server.address,
                 cfg.server.port);

    server->start();

    spdlog::info("Server shutdown complete.");

  } catch (const std::exception& e) {  // NOLINT
    // Catch-all for libraries that still use exceptions (e.g. Asio/STD
    // allocation errors)
    spdlog::critical("Fatal Unexpected Error: {}", e.what());
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
