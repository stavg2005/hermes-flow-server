#include "Listener.hpp"

#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast.hpp>
#include <expected>
#include <iostream>

#include "HttpSession.hpp"
#include "IoContextPool.hpp"
#include "Router.hpp"
#include "Types.hpp"
#include "spdlog/spdlog.h"

using namespace hermes::net::http;
namespace hermes::net {

std::expected<std::shared_ptr<Listener>, ErrorInfo> Listener::create(
    asio::io_context& main_ioc, std::shared_ptr<IoContextPool> pool,
    const tcp::endpoint& endpoint, std::shared_ptr<Router> router) {
  tcp::acceptor acceptor(main_ioc);

  auto run_step =
      [&](auto operation,
          const std::string& err_msg) -> std::expected<void, ErrorInfo> {
    beast::error_code ec;
    operation(ec);
    if (ec) {
      return std::unexpected(ErrorInfo::From(AppError::NetworkError,
                                             err_msg + ": " + ec.message()));
    }
    return {};
  };

  return run_step([&](auto& ec) { acceptor.open(endpoint.protocol(), ec); },
                  "Open Failed")
      .and_then([&] {
        return run_step(
            [&](auto& ec) {
              acceptor.set_option(asio::socket_base::reuse_address(true), ec);
            },
            "SetOption Failed");
      })
      .and_then([&] {
        return run_step([&](auto& ec) { acceptor.bind(endpoint, ec); },
                        "Bind Failed");
      })
      .and_then([&] {
        return run_step(
            [&](auto& ec) {
              acceptor.listen(asio::socket_base::max_listen_connections, ec);
            },
            "Listen Failed");
      })

      .transform([&] {
        spdlog::info("Listener bound to {}:{}", endpoint.address().to_string(),
                     endpoint.port());
        return std::shared_ptr<Listener>(new Listener(
            main_ioc, std::move(pool), std::move(acceptor), std::move(router)));
      });
}

Listener::Listener(asio::io_context& main_ioc,
                   std::shared_ptr<IoContextPool> pool,
                   tcp::acceptor&& acceptor, std::shared_ptr<Router> router)
    : main_ioc_(main_ioc),
      pool_(std::move(pool)),
      acceptor_(std::move(acceptor)),
      router_(std::move(router)) {}

void Listener::run() {
  spdlog::debug(("Starting to accept connections.. "));

  asio::co_spawn(
      acceptor_.get_executor(),
      [this, self = shared_from_this()]() { return do_accept(); },
      asio::detached);
}

asio::awaitable<std::expected<void, ErrorInfo>> Listener::do_accept() {
  try {
    for (;;) {
      auto& pool_ioc = pool_->get_io_context();

      auto [ec, socket] = co_await acceptor_.async_accept(
          pool_ioc, asio::as_tuple(asio::use_awaitable));

      if (ec) {
        if (ec == asio::error::operation_aborted) {
          break;
        }
        spdlog::error("listener faild to connect {}", ec.message());
        continue;
      }

      spdlog::debug("New connection accepted ");

      auto http_res = HttpSession::create(std::move(socket), router_);

      if (!http_res) {
        spdlog::error("{}", http_res.error().message);
      }
      auto http = http_res.value();

      http->run();
    }
  } catch (const std::exception& e) {
    spdlog::error("[Listener] Uncaught exception: {}", e.what());
    co_return std::unexpected(ErrorInfo::From(
        AppError::NetworkError, "[Listener] Uncaught exception: {}", e.what()));
  }
}
}  // namespace hermes::net
