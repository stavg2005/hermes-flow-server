// 1. Standard Library
#include <algorithm>
#include <csignal>
#include <exception>
#include <memory>
#include <thread>
#include <vector>

// 2. Third Party
#include <boost/asio.hpp>
#include <boost/beast/http.hpp>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

// 3. Local Headers
#include "NodeRegistry.hpp"
#include "Server.hpp"


using tcp = boost::asio::ip::tcp;

static void setup_logging() {
    std::vector<spdlog::sink_ptr> sinks;

    // A. Console Sink
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_level(spdlog::level::debug);
    sinks.push_back(console_sink);

    // B. Rotating File Sink (Max 5MB, 3 files)
    constexpr size_t MAX_SIZE = 1024 * 1024 * 5;
    constexpr size_t MAX_FILES = 3;
    auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        "logs/server.log", MAX_SIZE, MAX_FILES);
    file_sink->set_level(spdlog::level::trace);
    sinks.push_back(file_sink);

    // C. Register Logger
    auto logger = std::make_shared<spdlog::logger>("server", sinks.begin(), sinks.end());
    spdlog::register_logger(logger);
    spdlog::set_default_logger(logger);

    // D. Global Formatting
    spdlog::set_level(spdlog::level::debug);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [thread %t] %v");
    spdlog::flush_on(spdlog::level::debug);
}

int main(int argc, char* argv[]) {
    try {
        setup_logging();

        // 1. Initialize Node Types
        RegisterBuiltinNodes();

        // 2. Argument Validation
        // TODO: Use tomlplusplus for configuration
        if (argc != 3) {
            spdlog::critical("Usage: hermes_server <address> <port>");
            spdlog::critical("Example: hermes_server 0.0.0.0 8080");
            return EXIT_FAILURE;
        }

        auto* const address = argv[1];
        auto* const port = argv[2];
        auto const num_threads = std::max(1U, std::thread::hardware_concurrency());

        // 3. Server Setup
        asio::io_context main_ioc;
        auto server = std::make_shared<Server>(main_ioc, address, port, num_threads);

        // 4. Graceful Shutdown Signal
        asio::signal_set signals(main_ioc, SIGINT, SIGTERM);
        signals.async_wait([&server](const boost::system::error_code&, int signal_number) {
            spdlog::info("Stop signal ({}) received. Shutting down...", signal_number);
            server->Stop();
        });

        spdlog::info("Hermes Flow Server starting on {}:{}", address, port);

        // 5. Run
        server->Start();

        spdlog::info("Server shutdown complete.");

    } catch (const std::exception& e) {
        spdlog::critical("Fatal Error: {}", e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
