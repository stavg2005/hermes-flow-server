#pragma once

#include <boost/asio/io_context.hpp>
#include <memory>
#include <string>

#include "ActiveSessions.hpp"
#include "IoContextPool.hpp"
#include "Listener.hpp"
#include "Router.hpp"
#include "Types.hpp"
namespace hermes::net {
/**
 * @brief High-level HTTP Server Facade.
 */
class Server : public std::enable_shared_from_this<Server> {
 public:
  static std::expected<std::unique_ptr<Server>, ErrorInfo> create(
      boost::asio::io_context& main_ioc, const std::string& address,
      const std::string& port, unsigned int threads);

  ~Server();

  void start();
  void stop();

 private:
  Server(boost::asio::io_context& main_ioc, std::shared_ptr<IoContextPool> pool,
         std::shared_ptr<hermes::service::ActiveSessions> sessions,
         std::shared_ptr<http::Router> router,
         std::shared_ptr<Listener> listener);
  std::shared_ptr<IoContextPool> pool_;
  // active_sessions_ remains shared_ptr because it uses shared_from_this()
  // internally for async WebSocket handlers
  std::shared_ptr<hermes::service::ActiveSessions> active_sessions_;
  std::shared_ptr<http::Router> router_;
  std::shared_ptr<Listener> listener_;

  asio::io_context& main_io_;
};
}  // namespace hermes::net
