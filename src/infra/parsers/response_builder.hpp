#pragma once

#include <boost/beast.hpp>
#include <string>

#include "Types.hpp"
#include "boost/beast/http/message.hpp"
#include "boost/beast/http/status.hpp"
#include "boost/beast/version.hpp"
#include "boost/json.hpp"

namespace hermes::infra {
using res_t = boost::beast::http::response<boost::beast::http::string_body>;

class ResponseBuilder {
 private:
  static void set_standard_headers(res_t &res) {
    res.set(boost::beast::http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(boost::beast::http::field::access_control_allow_origin, "*");
    res.set(boost::beast::http::field::access_control_allow_methods,
            "GET, POST, PUT, DELETE, OPTIONS");
    res.set(boost::beast::http::field::access_control_allow_headers,
            "Content-Type, Authorization");
    res.set(boost::beast::http::field::access_control_max_age, "3600");
  }

 public:
  static void build_success_response(res_t &res, const std::string &sessionID,
                                     unsigned int version,
                                     bool keep_alive = false) {
    res.version(version);
    res.keep_alive(keep_alive);
    res.result(boost::beast::http::status::ok);

    set_standard_headers(res);
    res.set(boost::beast::http::field::content_type, "application/json");

    boost::json::object body_json;
    body_json["status"] = "success";
    body_json["message"] = "Session has started";
    body_json["sessionID"] = sessionID;

    res.body() = boost::json::serialize(body_json);
    res.prepare_payload();
  }

  static void build_success_response_with_id(res_t &res,
                                             const std::string &filename,
                                             size_t bytes_received,
                                             size_t bytes_processed,
                                             uint32_t file_id, unsigned version,
                                             bool keep_alive) {
    boost::json::object body_json;
    body_json["status"] = "success";
    body_json["message"] = "File uploaded successfully";
    body_json["filename"] = filename;
    body_json["file_id"] = file_id;
    body_json["bytes_received"] = bytes_received;
    body_json["bytes_processed"] = bytes_processed;

    make_json_response(res, boost::beast::http::status::ok, body_json, version,
                       keep_alive);
  }

  static void make_json_response(res_t &res, boost::beast::http::status status,
                                 const boost::json::value &val,
                                 unsigned int version, bool keep_alive) {
    res.result(status);
    res.version(version);
    res.keep_alive(keep_alive);

    set_standard_headers(res);
    res.set(boost::beast::http::field::content_type, "application/json");

    res.body() = boost::json::serialize(val);
    res.prepare_payload();
  }

  static void build_error_response(
      res_t &res, const std::string &error_message, unsigned int version,
      bool keep_alive = false,
      boost::beast::http::status status =
          boost::beast::http::status::bad_request) {
    res.version(version);
    res.keep_alive(keep_alive);
    res.result(status);

    set_standard_headers(res);
    res.set(boost::beast::http::field::content_type, "application/json");

    boost::json::object body_json;
    body_json["status"] = "error";
    body_json["message"] = error_message;

    res.body() = boost::json::serialize(body_json);
    res.prepare_payload();
  }

  static void build_plaintext_response(res_t &res, const std::string &body_text,
                                       unsigned int version,
                                       bool keep_alive = false) {
    res.version(version);
    res.keep_alive(keep_alive);
    res.result(boost::beast::http::status::ok);

    set_standard_headers(res);
    res.set(boost::beast::http::field::content_type,
            "text/plain; version=0.0.4");
    res.body() = body_text;
    res.prepare_payload();
  }

  static void build_options_response(res_t &res, unsigned int version,
                                     bool keep_alive = true) {
    res.version(version);
    res.keep_alive(keep_alive);
    res.result(boost::beast::http::status::ok);

    set_standard_headers(res);
    res.set(boost::beast::http::field::content_type, "text/plain");
    res.body() = "OK";
    res.prepare_payload();
  }
};

}  // namespace hermes::infra
