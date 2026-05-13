#include "Router.hpp"

#include <spdlog/spdlog.h>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/json.hpp>
#include <expected>
#include <string>
#include <sstream>

#include "Types.hpp"
#include "boost/beast/http/verb.hpp"

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

// Helper for extracting ID from request query params
std::expected<std::string, RouteError> extract_session_id(const req_t& req) {
  boost::urls::url_view url{req.target()};
  auto params = url.params();
  auto it = params.find("id");
  if (it == params.end()) {
    return std::unexpected(RouteError{beast::http::status::bad_request,
                                      "Missing query parameter: id"});
  }
  return std::string((*it)->value);
}
Router::Router(ActiveSessions& active, std::shared_ptr<IoContextPool> pool)
    : active_(active), pool_(std::move(pool)) {}

void Router::route_query(const req_t& req, res_t& res,
                         boost::beast::tcp_stream& stream) {
  http_requests_total_.fetch_add(1, std::memory_order_relaxed);
  // We maintain one try-catch ONLY for truly unexpected crashes
  try {
    boost::urls::url_view url{req.target()};
    std::string path{url.path()};

    auto match_route =
        [&](std::string_view prefix, beast::http::verb method,
            auto handler) -> std::optional<std::expected<void, RouteError>> {
      if (!path.starts_with(prefix)) return std::nullopt;

      if (req.method() != method) {
        return std::unexpected(RouteError{
            beast::http::status::method_not_allowed,
            "Method must be " + std::string(beast::http::to_string(method))});
      }
      return handler();
    };

    // clang-format off
    auto try_routes = [&]() -> std::expected<void, RouteError> {
      if (auto match = match_route("/transmit/", beast::http::verb::post, [&] { return handle_transmit(req, res); })) { return *match; }
      if(auto match = match_route("/secure_transmit/", beast::http::verb::post,[&]{return handle_secure_transmit(req,res);})){return *match;}
      if (auto match = match_route("/preview/", beast::http::verb::post, [&] { return handle_webrtc_request(req, res); })) { return *match; }
      if (auto match = match_route("/connect/", beast::http::verb::get, [&] { return handle_websocket_request(req, res, stream); })) { return *match; }
      if (auto match = match_route("/stop/", beast::http::verb::post, [&] { return handle_stop(req, res); })) { return *match; }
      if (auto match = match_route("/pause/", beast::http::verb::post, [&] { return handle_pause(req, res); })) { return *match; }
      if (auto match = match_route("/resume/", beast::http::verb::post, [&] { return handle_resume(req, res); })) { return *match; }
      if (auto match = match_route("/metrics", beast::http::verb::get, [&] { return handle_metrics(req, res); })) { return *match; }

      return std::unexpected(RouteError{beast::http::status::not_found, "Route not found"});
    };
    // clang-format on
    auto result = try_routes();

    if (!result) {
      auto err = result.error();
      spdlog::warn("API Error: {} - {}", std::to_underlying(err.code),
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

std::expected<void, RouteError> Router::process_session_request(
    const req_t& req, res_t& res, SessionType session_type,
    std::string_view endpoint_name) {
  spdlog::debug("Handling {} request", endpoint_name);

  boost::system::error_code jec;
  json::value jv = json::parse(req.body(), jec);

  // Note: checking jec first is correct, as jv might be invalid if jec is set.
  if (jec) {
    return std::unexpected(
        RouteError{beast::http::status::bad_request, "Invalid JSON format"});
  }
  if (!jv.is_object()) {
    return std::unexpected(RouteError{beast::http::status::bad_request,
                                      "JSON root must be an object"});
  }

  auto result = active_.create_session(jv.as_object(), session_type);

  if (!result) {
    return std::unexpected(
        RouteError{map_app_error(result.error().code), result.error().message});
  }

  ResponseBuilder::build_success_response(res, *result, req.version());
  return {};
}

std::expected<void, RouteError> Router::handle_transmit(const req_t& req,
                                                        res_t& res) {
  return process_session_request(req, res, SessionType::Standard, "/transmit");
}

std::expected<void, RouteError> Router::handle_secure_transmit(const req_t& req,
                                                               res_t& res) {
  return process_session_request(req, res, SessionType::StandartEncrypted,
                                 "/secure_transmit");
}

std::expected<void, RouteError> Router::handle_stop(const req_t& req,
                                                    res_t& res) {
  auto id_res = extract_session_id(req);
  if (!id_res) return std::unexpected(id_res.error());
  
  std::string id = *id_res;
  auto status = active_.remove_session(id);

  using enum ActiveSessions::SessionOpStatus;

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

std::expected<void, RouteError> Router::handle_pause(const req_t& req,
                                                     res_t& res) {
  auto id_res = extract_session_id(req);
  if (!id_res) return std::unexpected(id_res.error());
  
  std::string id = *id_res;
  auto status = active_.pause_session(id);

  using enum ActiveSessions::SessionOpStatus;

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

std::expected<void, RouteError> Router::handle_resume(const req_t& req,
                                                      res_t& res) {
  auto id_res = extract_session_id(req);
  if (!id_res) return std::unexpected(id_res.error());
  
  std::string id = *id_res;
  auto status = active_.resume_session(id);

  using enum ActiveSessions::SessionOpStatus;

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

std::expected<void, RouteError> Router::handle_webrtc_request(const req_t& req,
                                                              res_t& res) {
  return process_session_request(req, res, SessionType::WebRTC, "/webrtc");
}

std::expected<void, RouteError> Router::handle_websocket_request(
    const req_t& req, res_t& res, boost::beast::tcp_stream& stream) {
  if (!beast::websocket::is_upgrade(req)) {
    return std::unexpected(RouteError{beast::http::status::bad_request,
                                      "Request is not a WebSocket upgrade"});
  }

  auto id_res = extract_session_id(req);
  if (!id_res) return std::unexpected(id_res.error());
  
  std::string id = *id_res;
  spdlog::info("Attaching WebSocket to session: {}", id);

  auto result = active_.create_and_run_websocket_session(id, req, stream);

  if (!result) {
    return std::unexpected(
        RouteError{map_app_error(result.error().code), result.error().message});
  }

  return {};
}

std::expected<void, RouteError> Router::handle_metrics(const req_t& req,
                                                       res_t& res) {
  std::ostringstream oss;

  // Total HTTP Requests
  oss << "# HELP hermes_http_requests_total Total number of HTTP requests processed.\n"
      << "# TYPE hermes_http_requests_total counter\n"
      << "hermes_http_requests_total " << http_requests_total_.load(std::memory_order_relaxed) << "\n";

  // Total Sessions Created
  oss << "# HELP hermes_total_sessions_created_total Total number of sessions instantiated.\n"
      << "# TYPE hermes_total_sessions_created_total counter\n"
      << "hermes_total_sessions_created_total " << active_.get_total_sessions_created() << "\n";

  // Active Sessions
  oss << "# HELP hermes_active_sessions_total Number of currently active audio sessions.\n"
      << "# TYPE hermes_active_sessions_total gauge\n"
      << "hermes_active_sessions_total " << active_.size() << "\n";

  // Active WebSockets
  oss << "# HELP hermes_active_websockets_total Number of currently connected WebSockets.\n"
      << "# TYPE hermes_active_websockets_total gauge\n"
      << "hermes_active_websockets_total " << active_.get_active_websockets_count() << "\n";

  // Available WebRTC Ports
  oss << "# HELP hermes_available_webrtc_ports Number of WebRTC UDP ports currently available.\n"
      << "# TYPE hermes_available_webrtc_ports gauge\n"
      << "hermes_available_webrtc_ports " << active_.get_available_webrtc_ports_count() << "\n";

  // Per-session RTP stats
  auto stats = active_.get_all_session_rtp_stats();
  if (!stats.empty()) {
    oss << "# HELP hermes_rtp_bytes_sent_total Total RTP bytes sent by a session.\n"
        << "# TYPE hermes_rtp_bytes_sent_total counter\n";
    for (const auto& s : stats) {
      oss << "hermes_rtp_bytes_sent_total{session_id=\"" << s.id << "\"} " << s.bytes_sent << "\n";
    }

    oss << "# HELP hermes_rtp_packets_sent_total Total RTP packets sent by a session.\n"
        << "# TYPE hermes_rtp_packets_sent_total counter\n";
    for (const auto& s : stats) {
      oss << "hermes_rtp_packets_sent_total{session_id=\"" << s.id << "\"} " << s.packets_sent << "\n";
    }
  }

  ResponseBuilder::build_plaintext_response(res, oss.str(), req.version(), req.keep_alive());
  return {};
}

}  // namespace hermes::net::http
