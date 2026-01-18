#include "Router.hpp"

#include <spdlog/spdlog.h>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/json.hpp>
#include <string>

#include "Types.hpp"

using namespace hermes::service;
using namespace hermes::infra;
namespace hermes::net::http {
using req_t = beast::http::request<beast::http::string_body>;
using res_t = beast::http::response<beast::http::string_body>;

// Helper to map AppError (Business Logic) to HTTP Status
beast::http::status map_app_error(AppError err) {
  switch (err) {
    case AppError::ParseError:
      return beast::http::status::bad_request;  // 400
    case AppError::LogicError:
      return beast::http::status::conflict;  // 409
    case AppError::ConfigError:
      return beast::http::status::internal_server_error;  // 500
    case AppError::NetworkError:
      return beast::http::status::bad_gateway;  // 502
    case AppError::FileSystemError:
      return beast::http::status::internal_server_error;  // 500
    default:
      return beast::http::status::internal_server_error;  // 500
  }
}

// namespace

Router::Router(std::shared_ptr<ActiveSessions> active,
               std::shared_ptr<IoContextPool> pool)
    : active_(std::move(active)), pool_(std::move(pool)) {}

void Router::RouteQuery(const req_t& req, res_t& res,
                        boost::beast::tcp_stream& stream) {
  // We maintain one try-catch ONLY for truly unexpected crashes (like
  // std::bad_alloc) All logic errors are now handled via values.
  try {
    boost::urls::url_view url{req.target()};
    std::string path{url.path()};

    std::expected<void, RouteError> result;

    if (path.starts_with("/transmit/")) {
      if (req.method() != beast::http::verb::post) {
        result = std::unexpected(RouteError{beast::http::status::method_not_allowed,
                                            "Method must be POST"});
      } else {
        result = handle_transmit(req, res);
      }
    } else if (path.starts_with("/connect/")) {
      if (req.method() != beast::http::verb::get) {
        result = std::unexpected(
            RouteError{beast::http::status::method_not_allowed, "Method must be GET"});
      } else {
        result = handle_websocket_request(req, res, stream);
      }
    } else if (path.starts_with("/stop/")) {
      if (req.method() != beast::http::verb::post) {
        result = std::unexpected(RouteError{beast::http::status::method_not_allowed,
                                            "Method must be POST"});
      } else {
        result = handle_stop(req, res);
      }
    } else {
      // Path doesn't exist
      result = std::unexpected(
          RouteError{beast::http::status::not_found, "Route not found"});
    }

    if (!result) {
      auto err = result.error();
      spdlog::warn("API Error: {} - {}", static_cast<int>(err.code),
                   err.message);
      ResponseBuilder::build_error_response(res, err.message, req.version(),
                                            req.keep_alive(), err.code);
    }

  } catch (const std::exception& e) {
    spdlog::error("Routing Critical Error: {}", e.what());
    ResponseBuilder::build_error_response(
        res, "Internal Error: " + std::string(e.what()), req.version(),
        req.keep_alive(), beast::http::status::internal_server_error);
  }
}

std::expected<void, RouteError> Router::handle_transmit(const req_t& req,
                                                        res_t& res) {
  spdlog::debug("Handling /transmit request");

  boost::system::error_code jec;
  json::value jv = json::parse(req.body(), jec);

  if (jec) {
    return std::unexpected(
        RouteError{beast::http::status::bad_request, "Invalid JSON format"});
  }
  if (!jv.is_object()) {
    return std::unexpected(
        RouteError{beast::http::status::bad_request, "JSON root must be an object"});
  }

  auto result = active_->create_session(jv.as_object());

  if (!result) {
    return std::unexpected(
        RouteError{map_app_error(result.error().code), result.error().message});
  }

  ResponseBuilder::build_success_response(res, *result, req.version());
  return {};
}

std::expected<void, RouteError> Router::handle_stop(const req_t& req,
                                                    res_t& res) {
  boost::urls::url_view url{req.target()};
  auto params = url.params();
  auto it = params.find("id");

  if (it == params.end()) {
    return std::unexpected(
        RouteError{beast::http::status::bad_request, "Missing query parameter: id"});
  }

  std::string id((*it)->value);
  auto status = active_->remove_session(id);

  using enum ActiveSessions::RemoveStatus;

  switch (status) {
    case Success:
    case WebSocketNotFound:
      ResponseBuilder::build_success_response(res, id, req.version());
      return {};

    case SessionNotFound:
      return std::unexpected(
          RouteError{beast::http::status::not_found, "Session ID not found"});
  }
  return {};
}

std::expected<void, RouteError> Router::handle_websocket_request(
    const req_t& req, res_t& res, boost::beast::tcp_stream& stream) {
  if (!beast::websocket::is_upgrade(req)) {
    return std::unexpected(RouteError{beast::http::status::bad_request,
                                      "Request is not a WebSocket upgrade"});
  }

  boost::urls::url_view url{req.target()};
  auto params = url.params();
  auto it = params.find("id");

  if (it == params.end()) {
    return std::unexpected(
        RouteError{beast::http::status::bad_request, "Missing query parameter: id"});
  }

  std::string id((*it)->value);
  spdlog::info("Attaching WebSocket to session: {}", id);

  auto result = active_->create_and_run_WebsocketSession(id, req, stream);

  if (!result) {
    return std::unexpected(
        RouteError{map_app_error(result.error().code), result.error().message});
  }

  return {};
}
}  // namespace hermes::net
