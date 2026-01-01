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
 * * @details
 * **Architecture: One Acceptor, Many Workers**
 * - Runs on the `main_ioc` (Main Thread) to accept incoming TCP connections.
 * - **Load Balancing:** Upon acceptance, it requests a worker `io_context`
 * from the `io_context_pool`.
 * - **Handover:** It moves the connected socket to that worker context
 * (creating an `HttpSession`), ensuring that the heavy lifting (parsing,
 * audio streaming) happens on the worker threads, not the acceptor thread.
 */
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
