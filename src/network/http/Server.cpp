#include "Server.hpp"

#include <boost/asio.hpp>
#include <spdlog/spdlog.h>

#include "ActiveSessions.hpp"
#include "Listener.hpp"
#include "Router.hpp"
#include "IoContextPool.hpp"
#include "Types.hpp"
struct Server::Impl {
    asio::io_context& main_io_;

    // Components
    std::shared_ptr<IoContextPool> pool_;
    std::shared_ptr<ActiveSessions> active_sessions_;
    std::shared_ptr<Router> router_;
    std::shared_ptr<Listener> listener_;

    Impl(asio::io_context& io, const std::string& address, const std::string& port, unsigned int num_threads)//NOLINT
        : main_io_(io)
    {

        pool_ = std::make_shared<IoContextPool>(num_threads);


        active_sessions_ = std::make_shared<ActiveSessions>(pool_);


        router_ = std::make_shared<Router>(active_sessions_, pool_);


        tcp::endpoint endpoint{
            asio::ip::make_address(address),
            static_cast<unsigned short>(std::stoi(port))
        };

        listener_ = std::make_shared<Listener>(
            main_io_,
            *pool_,
            endpoint,
            router_
        );

        spdlog::info("Server initialized on {}:{} (Threads: {})", address, port, num_threads);
    }

    void Start() {
        pool_->run();
        listener_->run();
        main_io_.run();
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
