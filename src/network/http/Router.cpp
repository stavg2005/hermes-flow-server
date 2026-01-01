#include "Router.hpp"

#include <spdlog/spdlog.h>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/json.hpp>
#include <stdexcept>
#include <string>

#include "types.hpp"



using req_t = http::request<http::string_body>;
using res_t = http::response<http::string_body>;

namespace {

// Helper exception to carry HTTP status codes up the stack
struct HttpException : public std::runtime_error {
    http::status code;
    HttpException(http::status c, const std::string& msg) : std::runtime_error(msg), code(c) {}
};

}  

Router::Router(std::shared_ptr<ActiveSessions> active, std::shared_ptr<io_context_pool> pool)
    : active_(std::move(active)), pool_(std::move(pool)) {}

void Router::RouteQuery(const req_t& req, res_t& res, boost::beast::tcp_stream& stream) {
    try {
        boost::urls::url_view url{req.target()};
        std::string path{url.path()};

        if (path.starts_with("/transmit/")) {
            if (req.method() != http::verb::post) {
                throw HttpException(http::status::method_not_allowed, "Method must be POST");
            }
            handle_transmit(req, res);
        } else if (path.starts_with("/connect/")) {
            if (req.method() != http::verb::get) {
                throw HttpException(http::status::method_not_allowed, "Method must be GET");
            }
            handle_websocket_request(req, res, stream);
        } else if (path.starts_with("/stop/")) {
            if (req.method() != http::verb::post) {
                throw HttpException(http::status::method_not_allowed, "Method must be POST");
            }
            handle_stop(req, res);
        } else {
            // Path doesn't exist at all
            throw HttpException(http::status::not_found, "Route not found");
        }

    } catch (const HttpException& e) {
        // Handle expected API errors (404, 400, 405)
        spdlog::warn("API Error: {} - {}", static_cast<int>(e.code), e.what());
        ResponseBuilder::build_error_response(res, e.what(), req.version(), req.keep_alive(),
                                              e.code);

    } catch (const std::exception& e) {
        // Handle unexpected crashes (500)
        spdlog::error("Routing Critical Error: {}", e.what());
        ResponseBuilder::build_error_response(res, "Internal Server Error", req.version(),
                                              req.keep_alive(),
                                              http::status::internal_server_error);
    }
}

void Router::handle_transmit(const req_t& req, res_t& res) {
    spdlog::debug("Handling /transmit request");

    boost::system::error_code jec;
    json::value jv = json::parse(req.body(), jec);

    if (jec) {
        throw HttpException(http::status::bad_request, "Invalid JSON format");
    }
    if (!jv.is_object()) {
        throw HttpException(http::status::bad_request, "JSON root must be an object");
    }

    // Delegate to ActiveSessions
    auto id = active_->create_session(jv.as_object());

    ResponseBuilder::build_success_response(res, id, req.version());
}

void Router::handle_stop(const req_t& req, res_t& res) {
    boost::urls::url_view url{req.target()};
    auto params = url.params();
    auto it = params.find("id");

    if (it == params.end()) {
        throw HttpException(http::status::bad_request, "Missing query parameter: id");
    }

    std::string id((*it)->value);

    auto status = active_->remove_session(id);

    using enum ActiveSessions::RemoveStatus;

    switch (status) {
        case Success:
        case WebSocketNotFound:
            // Both cases are "Success" for the user (the session is stopped).
            // We can treat WebSocketNotFound as a 200 OK because the goal (stopping) was achieved.
            ResponseBuilder::build_success_response(res, id, req.version());
            break;

        case SessionNotFound:
            // Specific 404 error
            throw HttpException(http::status::not_found, "Session ID not found");
            break;
    }
}

void Router::handle_websocket_request(const req_t& req, res_t& res,
                                      boost::beast::tcp_stream& stream) {
    if (!beast::websocket::is_upgrade(req)) {
        throw HttpException(http::status::bad_request, "Request is not a WebSocket upgrade");
    }

    boost::urls::url_view url{req.target()};
    auto params = url.params();
    auto it = params.find("id");

    if (it == params.end()) {
        throw HttpException(http::status::bad_request, "Missing query parameter: id");
    }

    std::string id((*it)->value);
    spdlog::info("Attaching WebSocket to session: {}", id);

    try {
        active_->create_and_run_WebsocketSession(id, req, stream);
    } catch (const std::exception& e) {
        // Convert internal lookup failure to 404 if possible, otherwise let it bubble as 500
        throw HttpException(http::status::not_found,
                            "Session ID not found for WebSocket attachment");
    }
}
