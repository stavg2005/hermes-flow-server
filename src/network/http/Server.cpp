#include "Server.hpp"

#include <boost/asio.hpp>
#include <spdlog/spdlog.h>

#include "ActiveSessions.hpp"
#include "Listener.hpp"
#include "Router.hpp"
#include "io_context_pool.hpp"
#include "types.hpp"
struct Server::Impl {
    asio::io_context& main_io_;

    // Components
    std::shared_ptr<io_context_pool> pool_;
    std::shared_ptr<ActiveSessions> active_sessions_;
    std::shared_ptr<Router> router_;
    std::shared_ptr<listener> listener_;

    Impl(asio::io_context& io, const std::string& address, const std::string& port, unsigned int num_threads)//NOLINT
        : main_io_(io)
    {
        // 1. Thread Pool
        pool_ = std::make_shared<io_context_pool>(num_threads);

        // 2. Session Management
        active_sessions_ = std::make_shared<ActiveSessions>(pool_);

        // 3. Request Routing
        router_ = std::make_shared<Router>(active_sessions_, pool_);

        // 4. HTTP Listener
        tcp::endpoint endpoint{
            asio::ip::make_address(address),
            static_cast<unsigned short>(std::stoi(port))
        };

        listener_ = std::make_shared<listener>(
            main_io_,
            *pool_,
            endpoint,
            router_
        );

        spdlog::info("Server initialized on {}:{} (Threads: {})", address, port, num_threads);
    }

    void Start() {
        pool_->run();     // Start worker threads
        listener_->run(); // Start accepting connections
        main_io_.run();   // Start main thread loop
    }

    void Stop() {
        spdlog::info("Stopping server components...");
        pool_->stop();
        main_io_.stop();
    }
};

Server::Server(asio::io_context& io, const std::string& address, const std::string& port, unsigned int num_threads) //NOLINT
    : pImpl_(std::make_unique<Impl>(io, address, port, num_threads))
{}

Server::~Server() = default;

void Server::Start() { pImpl_->Start(); }
void Server::Stop()  { pImpl_->Stop(); }
