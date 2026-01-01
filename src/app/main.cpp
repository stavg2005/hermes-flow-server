// 1. Standard Library
#include <csignal>
#include <exception>
#include <memory>
#include <vector>

// 2. Third Party
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <boost/asio.hpp>
#include <boost/beast/http.hpp>

// 3. Local Headers
#include "NodeRegistry.hpp"
#include "Server.hpp"
#include "config.hpp"
#include "types.hpp"

static void setup_logging() {
    std::vector<spdlog::sink_ptr> sinks;

    // A. Console Sink
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_level(spdlog::level::debug);
    sinks.push_back(console_sink);

    // B. Rotating File Sink (Max 5MB, 3 files)
    constexpr size_t MAX_SIZE = 1024UZ * 1024 * 5;
    constexpr size_t MAX_FILES = 3;
    auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>("logs/server.log",
                                                                            MAX_SIZE, MAX_FILES);
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

/**
 * @brief Application Entry Point.
 * 1. Logging: Initialize Spdlog (Console + Rotating File).
 * 2. Config: Load `config.toml` (fail fast if missing).
 * 3. Registry: Register available Audio Nodes (Mixer, Delay, etc.).
 * 4. Server: Initialize the HTTP Server and Thread Pool.
 * 5. Signals: Attach SIGINT/SIGTERM handlers for graceful shutdown.
 * 6. Run: Block main thread until stop signal received.
 */
int main(int argc, char* argv[]) {
    try {
        setup_logging();


        AppConfig cfg = LoadConfig("../config.toml");


        RegisterBuiltinNodes();

        asio::io_context main_ioc;
        auto server = std::make_shared<Server>(main_ioc, cfg.server.address,
                                               std::to_string(cfg.server.port), cfg.server.threads);

        //Graceful Shutdown Signal
        asio::signal_set signals(main_ioc, SIGINT, SIGTERM);
        signals.async_wait([&server](const boost::system::error_code&, int signal_number) {
            spdlog::info("Stop signal ({}) received. Shutting down...", signal_number);
            server->Stop();
        });

        spdlog::info("Hermes Flow Server starting on {}:{}", cfg.server.address, cfg.server.port);


        server->Start();

        spdlog::info("Server shutdown complete.");

    } catch (const std::exception& e) {  // NOLINT
        spdlog::critical("Fatal Error: {}", e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
