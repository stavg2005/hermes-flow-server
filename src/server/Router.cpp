#include "Router.hpp"
#include "boost/beast/http/status.hpp"
#include "boost/beast/http/verb.hpp"
#include "boost/json.hpp"
#include "boost/url/url_view.hpp"
#include <cstdint>
#include <string>

Router::Router() {}

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
  try {
    res.version(req.version());
    res.keep_alive(false);
    res.result(http::status::ok);

    add_common_headers(res);
    res.set(http::field::content_type, "application/json");

    std::stringstream json;
    json << "Brody test ok!!!!" << "\"}";

    res.body() = json.str();
    res.set(http::field::content_length, std::to_string(res.body().size()));
    res.prepare_payload();
  } catch (const std::exception &e) {
  }
}
