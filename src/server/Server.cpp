#include "Server.hpp"

#include <memory>

#include "ActiveSessions.hpp"
#include "Listener.hpp"
#include "Router.hpp"
#include "boost/asio/io_context.hpp"
#include "io_context_pool.hpp"

using tcp = boost::asio::ip::tcp;

Server::Server(asio::io_context& io, const std::string& address, const std::string& port,
               unsigned int num_threads)
    : main_io_(io),
      pool_(std::make_shared<io_context_pool>(num_threads)),
      active_sessions_(std::make_shared<ActiveSessions>(pool_)),
      router_(std::make_shared<Router>(active_sessions_, pool_)),
      listener_(std::make_shared<listener>(
          main_io_, *pool_,
          tcp::endpoint{boost::asio::ip::make_address(address),
                        static_cast<unsigned short>(std::stoi(port))},
          router_)  // Passing reference to router, assuming listener takes Router&
      ) {
         spdlog::info("Listening on {}:{} with {} I/O threads.", address, port,
                     num_threads);
      }

void Server::Start() {
    pool_->run();
    listener_->run();
    main_io_.run();
}

void Server::Stop() {
    pool_->stop();

    main_io_.stop();
}
