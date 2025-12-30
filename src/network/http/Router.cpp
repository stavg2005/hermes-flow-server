#include "Router.hpp"

#include <spdlog/spdlog.h>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/json.hpp>
#include <stdexcept>
#include <string>

#include "types.hpp"

// Define type aliases for readability
using req_t = http::request<http::string_body>;
using res_t = http::response<http::string_body>;
Router::Router(std::shared_ptr<ActiveSessions> active,  // NOLINT
               std::shared_ptr<io_context_pool> pool)
    : active_(std::move(active)), pool_(std::move(pool)) {}

void Router::RouteQuery(const req_t& req, res_t& res,  // NOLINT
                        boost::beast::tcp_stream& stream) {
    try {
        boost::urls::url_view url{req.target()};
        std::string path{url.path()};  // NOLINT

        if (req.method() == http::verb::post && path.starts_with("/transmit/")) {  // NOLINT
            handle_transmit(req, res);
        } else if (req.method() == http::verb::get && path.starts_with("/connect/")) {
            handle_websocket_request(req, res, stream);
        } else if (req.method() == http::verb::post && path.starts_with("/stop/")) {
            handle_stop(url, res);
        } else {
            throw std::runtime_error("Route not found");
        }
    } catch (const std::exception& e) {
        spdlog::error("Routing Error: {}", e.what());

        http::status status = http::status::internal_server_error;
        std::string msg = e.what();

        if (msg == "Invalid JSON" || msg == "JSON root must be a json object") {  // NOLINT
            status = http::status::bad_request;
        } else if (msg == "Route not found") {
            status = http::status::not_found;
        }

        ResponseBuilder::build_error_response(res, msg, req.version(), req.keep_alive(), status);
    }
}

void Router::handle_transmit(const req_t& req, res_t& res) {
    spdlog::debug("Handling /transmit request");

    boost::system::error_code jec;
    json::value jv = json::parse(req.body(), jec);

    if (jec) {
        throw std::runtime_error("Invalid JSON");
    }
    if (!jv.is_object()) {
        throw std::runtime_error("JSON root must be an ojsonect");
    }
    // Delegate to ActiveSessions
    auto id = active_->create_session(jv.as_object());

    ResponseBuilder::build_success_response(res, id, req.version());
}

void Router::handle_websocket_request(const req_t& req, res_t& res,
                                      boost::beast::tcp_stream& stream) {
    if (!beast::websocket::is_upgrade(req)) {
        throw std::runtime_error("Request is not a WebSocket upgrade");
    }

    boost::urls::url_view url{req.target()};
    auto params = url.params();
    auto it = params.find("id");

    if (it == params.end()) {
        throw std::runtime_error("Missing query parameter: id");
    }

    std::string id((*it)->value);
    spdlog::info("Attaching WebSocket to session: {}", id);

    active_->create_and_run_WebsocketSession(id, req, stream);
}

void Router::handle_stop(boost::urls::url_view&, res_t&) {
    throw std::runtime_error("Stop endpoint not implemented.");
}
