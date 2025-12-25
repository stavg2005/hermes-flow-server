#include <boost/asio.hpp>
#include <boost/beast/http.hpp>
#include <csignal>
#include <exception>
#include <memory>
#include <thread>

#include "NodeFactory.hpp"
#include "Server.hpp"

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
    console_sink->set_level(spdlog::level::debug);  // CONSOLE_LEVEL
    sinks.push_back(console_sink);

    // 3. Create a Rotating File Sink
    constexpr size_t ONE_KILOBYTE = 1024;
    constexpr size_t MAX_LOG_FILE_SIZE_MB = 5;  // Maximum log file size in MB
    auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        "logs/server.log", ONE_KILOBYTE * ONE_KILOBYTE * MAX_LOG_FILE_SIZE_MB, 3);
    file_sink->set_level(spdlog::level::trace);  // FILE_LEVEL
    sinks.push_back(file_sink);

    // 4. Create and register the logger
    auto logger = std::make_shared<spdlog::logger>("server", sinks.begin(), sinks.end());
    spdlog::register_logger(logger);
    spdlog::set_default_logger(logger);

    // 5. Set global levels and format
    spdlog::set_level(spdlog::level::debug);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [thread %t] %v");

    spdlog::flush_on(spdlog::level::debug);
}
//to avoid linking issues because  RegisterBuilitnodes depdence on creation fuction in node Registry and we must initilize them at startup
//TODO check why bruh how does it find it
void RegisterBuiltinNodes();


int main(int argc, char* argv[]) {
    try {
        // Set up logging FIRST ---
        setup_logging();

        RegisterBuiltinNodes();

        // TODO use tomlplusplus for configuration instead of command line options
        //  --- 2. Argument Validation ---
        if (argc != 3) {
            spdlog::critical("Usage: hermes_server <address> <port>");
            spdlog::critical("Example: hermes_server 0.0.0.0 8080");
            return EXIT_FAILURE;
        }

        // --- 3. Configuration Parsing ---
        auto* const address = argv[1];
        auto* const port = argv[2];
        auto const num_threads = std::max(1U, std::thread::hardware_concurrency());

        net::io_context main_ioc;
        auto server = std::make_shared<Server>(main_ioc, address, port, num_threads);

        // Setup Graceful Shutdown
        net::signal_set signals(main_ioc, SIGINT, SIGTERM);
        signals.async_wait([&](auto, auto) {
            // This handler is NON-BLOCKING
            spdlog::info("Stop signal received...");
            server->Stop();
        });
        spdlog::trace("Signal set waiting for SIGINT/SIGTERM.");

        spdlog::info("Hermes Flow Server starting on {}:{}", address, port);
        server->Start();

        // --- 9. Shutdown Complete ---
        spdlog::info("Server shutdown complete bitch.");

    } catch (const std::exception& e) {
        spdlog::critical("Fatal Error: {}", e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
