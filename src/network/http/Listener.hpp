#pragma once

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <memory>

#include "Router.hpp"
#include "boost/asio/io_context.hpp"
#include "io_context_pool.hpp"
#include "types.hpp"

class ServerContext;


/**
 * @brief The TCP Connection Acceptor.
**/
class listener : public std::enable_shared_from_this<listener> {
   public:
    // Constructor takes io_context and endpoint to bind to
    listener(asio::io_context& ioc, io_context_pool& pool, const tcp::endpoint& endpoint,
             const std::shared_ptr<Router>& router);

    // Start accepting incoming connections
    void run();

   private:
    // Accept a new connection
    asio::awaitable<void> do_accept();

    // Member variables

    tcp::acceptor acceptor_;
    boost::asio::io_context& main_ioc_;
    io_context_pool& pool_;
    std::shared_ptr<Router> router_;
};
