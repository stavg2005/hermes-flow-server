#include "Listener.hpp"

#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast.hpp>
#include <iostream>

#include "Router.hpp"
#include "http_session.hpp"
#include "io_context_pool.hpp"
#include "spdlog/spdlog.h"
#include "types.hpp"

static void fail(beast::error_code ec, const char* what) {
    if (ec != beast::errc::not_connected && ec != asio::error::eof &&
        ec != asio::error::connection_reset) {
        spdlog::error(("{} : {}"), what, ec.message());
    }
}

listener::listener(asio::io_context& main_ioc, io_context_pool& pool, const tcp::endpoint& endpoint,
                   const std::shared_ptr<Router>& router)
    : acceptor_(main_ioc), main_ioc_(main_ioc), pool_(pool), router_(router) {
    beast::error_code ec;

    auto ec2 = acceptor_.open(endpoint.protocol(), ec);
    if (ec2) {
        fail(ec, "open");
        return;
    }
    // Enable SO_REUSEADDR to allow quick restart.
    acceptor_.set_option(asio::socket_base::reuse_address(true), ec);
    if (ec) {
        fail(ec, "set_option(reuse_address)");
        return;
    }

    ec2 = acceptor_.bind(endpoint, ec);
    if (ec2) {
        fail(ec, "bind");
        return;
    }

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

    asio::co_spawn(
        acceptor_.get_executor(), [this, self = shared_from_this()]() { return do_accept(); },
        asio::detached);
}

asio::awaitable<void> listener::do_accept() {
    try {
        for (;;) {
            auto& pool_ioc = pool_.get_io_context();

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

            std::make_shared<server::core::HttpSession>(std::move(socket), router_)->run();
        }
    } catch (const std::exception& e) {
        spdlog::error("[Listener] Uncaught exception: {}", e.what());
    } catch (...) {
        spdlog::error("[Listener] Unknown crash exception");
    }
}
