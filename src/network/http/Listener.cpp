#include "Listener.hpp"

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <iostream>

#include "Router.hpp"
#include "http_session.hpp"  // Make sure this path is correct
#include "io_context_pool.hpp"
#include "spdlog/spdlog.h"

// --- Add these new includes for coroutines ---
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>
// ---
#include "types.hpp"

// Report a failure
static void fail(beast::error_code ec, const char* what) {
    if (ec != beast::errc::not_connected && ec != asio::error::eof &&
        ec != asio::error::connection_reset) {
        spdlog::error(("{} : {}"), what, ec.message());
    }
}

// Constructor is unchanged
listener::listener(asio::io_context& main_ioc, io_context_pool& pool, const tcp::endpoint& endpoint,
                   const std::shared_ptr<Router>& router)
    : acceptor_(main_ioc), main_ioc_(main_ioc), pool_(pool), router_(router) {
    beast::error_code ec;

    // Open the acceptor
    auto ec2 = acceptor_.open(endpoint.protocol(), ec);
    if (ec2) {
        fail(ec, "open");
        return;
    }
    // Set SO_REUSEADDR to allow immediate reuse of the port after the server stops.
    // This prevents the "Address already in use" error caused by the OS keeping the port
    // in a TIME_WAIT state for a few minutes after the process exits.
    // Crucial for development and quick restarts.
    acceptor_.set_option(asio::socket_base::reuse_address(true), ec);
    if (ec) {
        fail(ec, "set_option(reuse_address)");
        return;
    }

    // Bind to the server address
    ec2 = acceptor_.bind(endpoint, ec);
    if (ec2) {
        fail(ec, "bind");
        return;
    }

    // Start listening for connections with backlog queue with the max size
    ec2 = acceptor_.listen(asio::socket_base::max_listen_connections, ec);
    if (ec2) {
        fail(ec, "listen");
        return;
    }
    spdlog::debug(("Listener successfully bound to {} {} "), endpoint.address().to_string(),
                  endpoint.port());
}

void listener::run() {
    spdlog::debug(("Starting to accept connections.. "));

    // fire and forget corutine and continune listening
    asio::co_spawn(
        acceptor_.get_executor(), [this, self = shared_from_this()]() { return do_accept(); },
        asio::detached);
}

asio::awaitable<void> listener::do_accept() {
    try {
        for (;;) {
            // get an io_context from the pool for the future sessions socket
            auto& pool_ioc = pool_.get_io_context();

            // suspends until a connection attempted  has been detected ,upon aconnection a new
            // socket that is connected to the client would be returned
            auto [ec, socket] =
                co_await acceptor_.async_accept(pool_ioc, asio::as_tuple(asio::use_awaitable));

            if (ec) {
                if (ec == asio::error::operation_aborted) {
                    break;
                }
                fail(ec, "accept");
                continue;
            }

            spdlog::debug("New connection accepted ");

            // 4. Create the session.
            std::make_shared<server::core::HttpSession>(
                std::move(socket),  // The socket is already on a pool ioc
                router_             // Pass the router
                )
                ->run();
        }
    } catch (const std::exception& e) {
        spdlog::error("[Listener] Uncaught exception: {}", e.what());
    } catch (...) {
        spdlog::error("[Listener] Unknown crash exception");
    }
}
