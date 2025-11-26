#pragma once
#include <boost/asio.hpp>
#include <memory>
#include <string>

#include "ActiveSessions.hpp"
#include "Listener.hpp"
#include "Router.hpp"
#include "boost/asio/io_context.hpp"
#include "io_context_pool.hpp"

namespace asio = boost::asio;
class Server : std::enable_shared_from_this<Server> {
   public:
    Server(asio::io_context& io, const std::string& address, const std::string &port, unsigned int num_threads);
    void Start();
    void Stop();
   private:
    asio::io_context& main_io_;
    std::shared_ptr<io_context_pool> pool_;
    std::shared_ptr<ActiveSessions> active_sessions_;
    const std::shared_ptr<Router> router_;
    std::shared_ptr<listener> listener_;
};
