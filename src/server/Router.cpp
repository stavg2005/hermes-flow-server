#include "Router.hpp"
#include "boost/beast/http/status.hpp"
#include "boost/beast/http/verb.hpp"
#include "boost/json.hpp"
#include "boost/json/object.hpp"
#include "boost/url/url_view.hpp"
#include "models/Nodes.hpp"
#include "utils/Json2Graph.hpp"
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
namespace sys = boost::system;

void Router::RouteQuery(const http::request<http::string_body> &req,
                        http::response<http::string_body> &res) {
  try {
    boost::urls::url_view url{req.target()};
    std::string path{url.path()};

    if (path.starts_with("/transmit/") && req.method() == http::verb::post) {
      HandleTransmitRequest(req, res);
    } else if (path.starts_with("/stop/") && req.method() == http::verb::post) {
      HandleStopRequest(url, res);
    }
    ResponseBuilder::add_cors_headers(res);

  } catch (const std::exception &e) {
    ResponseBuilder::build_error_response(
        res, std::string("Router error: ") + e.what(), req.version(), false,
        http::status::internal_server_error);
  }
}

void Router::HandleStopRequest(boost::urls::url_view url,
                               http::response<http::string_body> &res) {}

static void add_common_headers(http::response<http::string_body> &res) {
  res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
  res.set(http::field::content_type, "application/json");
  res.set(http::field::access_control_allow_origin, "*");
  res.set(http::field::access_control_allow_methods,
          "GET, POST, PUT, DELETE, OPTIONS");
  res.set(http::field::access_control_allow_headers,
          "Content-Type, Authorization");
}

void Router::HandleTransmitRequest(const http::request<http::string_body> &req,
                                   http::response<http::string_body> &res) {
  res.version(req.version());
  res.keep_alive(false);
  add_common_headers(res);

  try {
    // Parse JSON safely
    sys::error_code jec;
    bj::value jv = bj::parse(req.body(), jec);
    if (jec) {
      res.result(http::status::bad_request);
      res.set(http::field::content_type, "application/json");
      res.body() = bj::serialize(bj::object{
          {"ok", false}, {"error", "Invalid JSON"}, {"detail", jec.message()}});
      res.prepare_payload();
      return;
    }

    if (!jv.is_object()) {
      res.result(http::status::bad_request);
      res.set(http::field::content_type, "application/json");
      res.body() = bj::serialize(
          bj::object{{"ok", false}, {"error", "JSON root must be an object"}});
      res.prepare_payload();
      return;
    }

    bj::object &jobj = jv.as_object();

    // Convert JSON -> Graph
    Graph g = parse_graph(jobj);

    auto id = active_->create_and_run_session(g);
    auto id = session->id();

    session->set_graph(std::make_shared<Graph>(g));

    session->start(); // non-blocking

    // Respond JSON
    res.result(http::status::ok);
    res.set(http::field::content_type, "application/json");
    res.body() = bj::serialize(bj::object{{"ok", true}, {"session_id", id}});
    res.prepare_payload();
  } catch (const std::exception &e) {
    res.result(http::status::internal_server_error);
    res.set(http::field::content_type, "application/json");
    res.body() = bj::serialize(bj::object{
        {"ok", false}, {"error", "Server error"}, {"detail", e.what()}});
    res.prepare_payload();
  }
}