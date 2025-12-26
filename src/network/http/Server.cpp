#include "Server.hpp"

// Component headers moved here
#include "ActiveSessions.hpp"
#include "Listener.hpp"
#include "Router.hpp"
#include "io_context_pool.hpp"
#include "spdlog/spdlog.h"
#include "types.hpp"

struct Server::Impl {
    asio::io_context& main_io_;

    // Server components
    std::shared_ptr<io_context_pool> pool_;
    std::shared_ptr<ActiveSessions> active_sessions_;
    std::shared_ptr<Router> router_;
    std::shared_ptr<listener> listener_;
    // TODO get iocontext from pool
    Impl(asio::io_context& io, const std::string& address, const std::string& port,
         unsigned int num_threads)
        : main_io_(io) {
        // 1. Initialize Thread Pool
        pool_ = std::make_shared<io_context_pool>(num_threads);

        // 2. Initialize Session Manager
        active_sessions_ = std::make_shared<ActiveSessions>(pool_);

        // 3. Initialize Router
        router_ = std::make_shared<Router>(active_sessions_, pool_);

        // 4. Initialize Listener
        auto endpoint = tcp::endpoint{asio::ip::make_address(address),
                                      static_cast<unsigned short>(std::stoi(port))};

        listener_ = std::make_shared<listener>(main_io_, *pool_, endpoint, router_);

        spdlog::info("Listening on {}:{} with {} I/O threads.", address, port, num_threads);
    }

    void Start() {
        pool_->run();
        listener_->run();
        main_io_.run();
    }

    void Stop() {
        pool_->stop();
        main_io_.stop();
    }
};

Server::Server(asio::io_context& io, const std::string& address, const std::string& port,
               unsigned int num_threads)
    : pImpl_(std::make_unique<Impl>(io, address, port, num_threads)) {}

Server::~Server() = default;

void Server::Start() {
    pImpl_->Start();
}

void Server::Stop() {
    pImpl_->Stop();
}
