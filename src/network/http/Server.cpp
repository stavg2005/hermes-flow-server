#include "Server.hpp"

#include <spdlog/spdlog.h>

#include <boost/asio.hpp>
#include <memory>

Server::Server(asio::io_context& io, const std::string& address, const std::string& port,
               unsigned int num_threads)
    : main_io_(io) {
    pool_ = std::make_shared<IoContextPool>(num_threads);

    active_sessions_ = std::make_shared<ActiveSessions>(pool_);

    router_ = std::make_shared<Router>(active_sessions_, pool_);

    tcp::endpoint endpoint{asio::ip::make_address(address),
                           static_cast<unsigned short>(std::stoi(port))};

    listener_ = std::make_shared<Listener>(main_io_, *pool_, endpoint, router_);

    spdlog::info("Server initialized on {}:{} (Threads: {})", address, port, num_threads);
}

void Server::Start() {
    pool_->run();
    listener_->run();
    main_io_.run();
}

void Server::Stop() {
    spdlog::info("Stopping server components...");
    pool_->stop();
    main_io_.stop();
}

Server::~Server() = default;
