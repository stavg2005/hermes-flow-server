#include "Server.hpp"

#include <spdlog/spdlog.h>

#include <boost/asio.hpp>
#include <memory>

using namespace hermes::service;
using namespace hermes::net::http;
namespace hermes::net {
std::expected<std::unique_ptr<Server>, ErrorInfo> Server::create(
    boost::asio::io_context& main_ioc, const hermes::config::AppConfig& cfg) {
  auto pool_result = IoContextPool::create(cfg.server.threads);
  if (!pool_result) {
    return std::unexpected(pool_result.error());
  }
  auto pool = std::move(*pool_result);

  auto active_sessions = std::make_shared<ActiveSessions>(*pool, cfg);
  auto router = std::make_shared<Router>(*active_sessions, pool);

  boost::asio::ip::tcp::endpoint endpoint;
  try {
    auto const addr = boost::asio::ip::make_address(cfg.server.address);
    auto const p = static_cast<unsigned short>(cfg.server.port);
    endpoint = boost::asio::ip::tcp::endpoint{addr, p};
  } catch (const std::exception& e) {
    return std::unexpected(
        ErrorInfo::From(AppError::ConfigError,
                        "Invalid Address/Port: " + std::string(e.what())));
  }

  auto listener_result = Listener::create(main_ioc, *pool, endpoint, router);
  if (!listener_result) {
    return std::unexpected(listener_result.error());
  }
  auto listener = std::move(*listener_result);

  auto server = std::unique_ptr<Server>(
      new Server(main_ioc, std::move(pool), std::move(active_sessions),
                 std::move(router), std::move(listener)));

  spdlog::info("Server initialized on {}:{}", cfg.server.address,
               cfg.server.port);
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

void Server::start() {
  pool_->run();
  listener_->run();
  main_io_.run();
}

void Server::stop() {
  spdlog::info("Stopping server components...");
  pool_->stop();
  main_io_.stop();
}

Server::~Server() = default;
}  // namespace hermes::net
