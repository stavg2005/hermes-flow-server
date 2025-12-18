#include "Router.hpp"

#include <memory>
#include <stdexcept>  // For std::runtime_error
#include <string>

#include "S3Client.hpp"
#include "boost/asio/io_context.hpp"
#include "boost/beast/http/status.hpp"
#include "boost/beast/http/verb.hpp"  // Use primary boost/json.hpp
#include "boost/beast/websocket/rfc6455.hpp"
#include "boost/url/url_view.hpp"
#include "spdlog/spdlog.h"

namespace sys = boost::system;
namespace bj = boost::json;

// Define type aliases for readability
using req_t = http::request<http::string_body>;
using res_t = http::response<http::string_body>;

void Router::RouteQuery(const req_t& req, res_t& res, boost::beast::tcp_stream& stream) {
    try {
        boost::urls::url_view url{req.target()};
        std::string path{url.path()};

        if (path.starts_with("/transmit/") && req.method() == http::verb::post) {
            handle_transmit(req, res);
        } else if (path.starts_with("/stop/") && req.method() == http::verb::post) {
            handle_stop(url, res);
        } else if (path.starts_with("/download/") && req.method() == http::verb::post) {
            handle_download(url, res);
        } else if (path.starts_with("/connect/") && req.method() == http::verb::get) {
            handle_websocket_request(req, res, stream);
        }

        else {
            throw std::runtime_error("Route not found");
        }
    } catch (const std::exception& e) {
        http::status status_code = http::status::internal_server_error;
        if (std::string(e.what()) == "Invalid JSON" ||
            std::string(e.what()) == "JSON root must be an object") {
            status_code = http::status::bad_request;
        } else if (std::string(e.what()) == "Route not found") {
            status_code = http::status::not_found;
        }
        spdlog::error(e.what());
        ResponseBuilder::build_error_response(res, e.what(), req.version(),
                                              req.keep_alive(),  // Pass keep_alive
                                              status_code);
    }
}

// --- Handler Functions ---

void Router::handle_stop(boost::urls::url_view& url, res_t& res) {
    throw std::runtime_error("Stop endpoint is not implemented");
}

void Router::handle_download(boost::urls::url_view& url, res_t& res) {
    spdlog::info("in handle_download");

    auto params = url.params();
    if (!params.contains("file_name")) {
        throw std::runtime_error("Missing file_name");
    }
    spdlog::info("request contains ");
    auto it = params.find("file_name");

    std::string file_name((*it)->value);
    auto& ioc = pool_->get_io_context();
    try {
        spdlog::info("Creating S3 session for file: {}", file_name);
        auto session = std::make_shared<S3Session>(ioc);
        spdlog::info("S3Session created");
        session->RequestFile(file_name);
        spdlog::info("RequestFile called");
    } catch (const std::exception& e) {
        spdlog::critical("Crash in S3 setup: {}", e.what());
    } catch (...) {
        spdlog::critical("Unknown crash in S3 setup");
    }
}

void Router::handle_transmit(const req_t& req, res_t& res) {
    //  Parse JSON
    spdlog::debug("in handle transmit");
    sys::error_code jec;
    bj::value jv = bj::parse(req.body(), jec);
    if (jec) {
        throw std::runtime_error("Invalid JSON");
    }

    // Validate JSON structure
    if (!jv.is_object()) {
        throw std::runtime_error("JSON root must be an object");
    }

    auto id = active_->create_session(jv.as_object());
    spdlog::debug("In router created session with id {}", id);
    ResponseBuilder::build_success_response(res, id, req.version());
}
void Router::handle_websocket_request(const req_t& req, res_t& res,
                                      boost::beast::tcp_stream& stream) {
    if (!beast::websocket::is_upgrade(req)) {
        throw std::runtime_error("Request Must be Upgradable");
    }

    spdlog::info("got a websocket request");
    boost::urls::url_view url{req.target()};

    auto params = url.params();
    if (!params.contains("id")) {
        throw std::runtime_error("Missing ID");
    }

    auto it = params.find("id");

    std::string id((*it)->value);

    active_->create_and_run_WebsocketSession(id, req, stream);
}
