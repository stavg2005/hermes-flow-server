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
  static std::expected<std::shared_ptr<Server>, ErrorInfo> Create(
      boost::asio::io_context& main_ioc, const std::string& address,
      const std::string& port, unsigned int threads);

  ~Server();

  void Start();
  void Stop();

 private:
  Server(boost::asio::io_context& main_ioc, std::shared_ptr<IoContextPool> pool,
         std::shared_ptr<hermes::service::ActiveSessions> sessions,
         std::shared_ptr<Router> router, std::shared_ptr<Listener> listener);
  std::shared_ptr<IoContextPool> pool_;
  std::shared_ptr<hermes::service::ActiveSessions> active_sessions_;
  std::shared_ptr<Router> router_;
  std::shared_ptr<Listener> listener_;

  asio::io_context& main_io_;
};
}  // namespace hermes::net
