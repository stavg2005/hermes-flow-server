#include "Router.hpp"

#include <stdexcept>  // For std::runtime_error
#include <string>

#include "boost/beast/http/status.hpp"
#include "boost/beast/http/verb.hpp"
#include "boost/json.hpp"  // Use primary boost/json.hpp
#include "boost/url/url_view.hpp"
#include "models/Nodes.hpp"
#include "spdlog/spdlog.h"
#include "utils/Json2Graph.hpp"

namespace sys = boost::system;
namespace bj = boost::json;

// Define type aliases for readability
using req_t = http::request<http::string_body>;
using res_t = http::response<http::string_body>;

void Router::RouteQuery(const req_t &req, res_t &res) {
  try {
    boost::urls::url_view url{req.target()};
    std::string path{url.path()};

    if (path.starts_with("/transmit/") && req.method() == http::verb::post) {
      handle_transmit(req, res);
    } else if (path.starts_with("/stop/") && req.method() == http::verb::post) {
      handle_stop(url, res);
    } else {
      throw std::runtime_error("Route not found");
    }

  } catch (const std::exception &e) {
    http::status status_code = http::status::internal_server_error;
    if (std::string(e.what()) == "Invalid JSON" ||
        std::string(e.what()) == "JSON root must be an object") {
      status_code = http::status::bad_request;
    } else if (std::string(e.what()) == "Route not found") {
      status_code = http::status::not_found;
    }
    spdlog::error((e.what()));
    ResponseBuilder::build_error_response(res, e.what(), req.version(),
                                          req.keep_alive(),  // Pass keep_alive
                                          status_code);
  }
}

// --- Handler Functions ---

void Router::handle_stop(boost::urls::url_view url, res_t &res) {
  throw std::runtime_error("Stop endpoint is not implemented");
}

void Router::handle_transmit(const req_t &req, res_t &res) {
  //  Parse JSON
  spdlog::debug(("in handle transmit"));
  sys::error_code jec;
  bj::value jv = bj::parse(req.body(), jec);
  if (jec) {
    throw std::runtime_error("Invalid JSON");
  }

  // Validate JSON structure
  if (!jv.is_object()) {
    throw std::runtime_error("JSON root must be an object");
  }
  bj::object &jobj = jv.as_object();

  // Business Logic
  Graph g = parse_graph(jobj);
  print_graph(g);
  auto id = active_->create_and_run_session(std::move(g));

  ResponseBuilder::build_success_response(res, id, req.version());
}
