#include "Listener.hpp"

#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast.hpp>
#include <iostream>

#include "HttpSession.hpp"
#include "IoContextPool.hpp"
#include "Router.hpp"
#include "Types.hpp"
#include "spdlog/spdlog.h"

using namespace hermes::net::http;
namespace hermes::net {

std::expected<std::shared_ptr<Listener>, ErrorInfo> Listener::Create(
    asio::io_context& main_ioc, std::shared_ptr<IoContextPool> pool,
    const tcp::endpoint& endpoint, std::shared_ptr<Router> router) {
  beast::error_code ec;

  tcp::acceptor acceptor(main_ioc);

  acceptor.open(endpoint.protocol(), ec);
  if (ec) {
    return std::unexpected(ErrorInfo::From(
        AppError::NetworkError, "Listener Open Failed: " + ec.message()));
  }

  acceptor.set_option(asio::socket_base::reuse_address(true), ec);
  if (ec) {
    return std::unexpected(ErrorInfo::From(
        AppError::NetworkError, "Listener SetOption Failed: " + ec.message()));
  }

  acceptor.bind(endpoint, ec);
  if (ec) {
    // This is the most common error (Port already in use)
    return std::unexpected(ErrorInfo::From(
        AppError::NetworkError,
        "Listener Bind Failed (" + endpoint.address().to_string() + ":" +
            std::to_string(endpoint.port()) + "): " + ec.message()));
  }

  acceptor.listen(asio::socket_base::max_listen_connections, ec);
  if (ec) {
    return std::unexpected(ErrorInfo::From(
        AppError::NetworkError, "Listener Listen Failed: " + ec.message()));
  }

  spdlog::info("Listener successfully bound to {}:{}",
               endpoint.address().to_string(), endpoint.port());

  // 6. Create Instance (Move the ready acceptor into the object)
  // Using 'new' to access private constructor
  return std::shared_ptr<Listener>(new Listener(
      main_ioc, std::move(pool), std::move(acceptor), std::move(router)));
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

asio::awaitable<void> Listener::do_accept() {
  try {
    for (;;) {
      auto& pool_ioc = pool_->get_io_context();

      auto [ec, socket] = co_await acceptor_.async_accept(
          pool_ioc, asio::as_tuple(asio::use_awaitable));

      if (ec) {
        if (ec == asio::error::operation_aborted) {
          break;
        }
        // fail(ec, "accept");
        continue;
      }

      spdlog::debug("New connection accepted ");

      auto http_res = HttpSession::Create(std::move(socket), router_);

      if (!http_res) {
        spdlog::error("bruhhh");
      }
      auto http = http_res.value();

      http->run();
    }
  } catch (const std::exception& e) {
    spdlog::error("[Listener] Uncaught exception: {}", e.what());
  } catch (...) {
    spdlog::error("[Listener] Unknown crash exception");
  }
}
}  // namespace hermes::net
