#include "Server.hpp"

#include <spdlog/spdlog.h>

#include <boost/asio.hpp>
#include <memory>

using namespace hermes::service;
using namespace hermes::net::http;
namespace hermes::net {
std::expected<std::shared_ptr<Server>, ErrorInfo> Server::Create(
    boost::asio::io_context& main_ioc, const std::string& address,
    const std::string& port, unsigned int threads) {
  auto pool_result = IoContextPool::Create(threads);
  if (!pool_result) {
    return std::unexpected(pool_result.error());
  }
  auto pool = std::move(*pool_result);

  auto active_sessions = std::make_shared<ActiveSessions>(pool);
  auto router = std::make_shared<Router>(active_sessions, pool);

  boost::asio::ip::tcp::endpoint endpoint;
  try {
    auto const addr = boost::asio::ip::make_address(address);
    auto const p = static_cast<unsigned short>(std::stoi(port));
    endpoint = boost::asio::ip::tcp::endpoint{addr, p};
  } catch (const std::exception& e) {
    return std::unexpected(
        ErrorInfo::From(AppError::ConfigError,
                        "Invalid Address/Port: " + std::string(e.what())));
  }

  auto listener_result = Listener::Create(main_ioc, pool, endpoint, router);
  if (!listener_result) {
    return std::unexpected(listener_result.error());
  }
  auto listener = std::move(*listener_result);

  auto server = std::shared_ptr<Server>(
      new Server(main_ioc, std::move(pool), std::move(active_sessions),
                 std::move(router), std::move(listener)));

  spdlog::info("Server initialized on {}:{}", address, port);
  return server;
}

Server::Server(boost::asio::io_context& main_ioc,
               std::shared_ptr<IoContextPool> pool,
               std::shared_ptr<ActiveSessions> sessions,
               std::shared_ptr<Router> router,
               std::shared_ptr<Listener> listener)
    : main_io_(main_ioc),
      pool_(std::move(pool)),
      active_sessions_(std::move(sessions)),
      router_(std::move(router)),
      listener_(std::move(listener)) {}

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
}  // namespace hermes::net
