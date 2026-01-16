#pragma once

#include <boost/asio/io_context.hpp>
#include <memory>
#include <string>

#include "ActiveSessions.hpp"
#include "IoContextPool.hpp"
#include "Listener.hpp"
#include "Router.hpp"
#include "Types.hpp"

/**
 * @brief High-level HTTP Server Facade.
 *    Uses PIMPL for ABI stability
 */
class Server : public std::enable_shared_from_this<Server> {
   public:
    Server(boost::asio::io_context& io, const std::string& address, const std::string& port,
           unsigned int num_threads);
    ~Server();

    void Start();
    void Stop();

   private:
    std::shared_ptr<IoContextPool> pool_;
    std::shared_ptr<ActiveSessions> active_sessions_;
    std::shared_ptr<Router> router_;
    std::shared_ptr<Listener> listener_;

    asio::io_context& main_io_;
};
