#pragma once

#include "boost/beast/http/message.hpp" // Use full header
#include "boost/beast/http/status.hpp"
#include "boost/beast/version.hpp"
#include "boost/json.hpp"
#include <boost/beast.hpp>
#include <string>

namespace server::models {
namespace json = boost::json;
namespace http = boost::beast::http;

// Define this alias for readability
using res_t = http::response<http::string_body>;

class ResponseBuilder {
private:

  static void set_standard_headers(res_t &res) {
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::access_control_allow_origin, "*");
    res.set(http::field::access_control_allow_methods,
            "GET, POST, PUT, DELETE, OPTIONS");
    res.set(http::field::access_control_allow_headers,
            "Content-Type, Authorization");
    res.set(http::field::access_control_max_age, "3600");
  }

public:

  static void build_success_response(res_t &res,
                                     const std::string &sessionID,
                                     unsigned int version,
                                     bool keep_alive = false) {

    res.version(version);
    res.keep_alive(keep_alive);
    res.result(http::status::ok);

    set_standard_headers(res); 
    res.set(http::field::content_type, "application/json");


    json::object body_json;
    body_json["status"] = "success";
    body_json["message"] = "Session has started";
    body_json["sessionID"] = sessionID;

    res.body() = json::serialize(body_json);
    res.prepare_payload();
  }


  static void build_success_response_with_id(res_t &res,
                                             const std::string &filename,
                                             size_t bytes_received,
                                             size_t bytes_processed,
                                             uint32_t file_id,
                                             unsigned version,
                                             bool keep_alive) {

    json::object body_json;
    body_json["status"] = "success";
    body_json["message"] = "File uploaded successfully";
    body_json["filename"] = filename;
    body_json["file_id"] = file_id;
    body_json["bytes_received"] = bytes_received;
    body_json["bytes_processed"] = bytes_processed;

    // Just call the generic helper
    make_json_response(res, http::status::ok, body_json, version, keep_alive);
  }


  static void make_json_response(res_t &res,
                                 http::status status,
                                 const boost::json::value &val,
                                 unsigned int version,
                                 bool keep_alive) {
    res.result(status);
    res.version(version);
    res.keep_alive(keep_alive);

    set_standard_headers(res); // Use new helper
    res.set(http::field::content_type, "application/json");

    res.body() = boost::json::serialize(val);
    res.prepare_payload();
  }


  static void build_error_response(res_t &res,
                                   const std::string &error_message,
                                   unsigned int version, bool keep_alive = false,
                                   http::status status = http::status::bad_request) {

    res.version(version);
    res.keep_alive(keep_alive);
    res.result(status);

    set_standard_headers(res); // Use new helper
    res.set(http::field::content_type, "application/json");

    // --- USE BOOST::JSON ---
    json::object body_json;
    body_json["status"] = "error";
    body_json["message"] = error_message;

    res.body() = json::serialize(body_json);
    res.prepare_payload();
  }


  static void build_options_response(res_t &res,
                                     unsigned int version,
                                     bool keep_alive = true) {
    res.version(version);
    res.keep_alive(keep_alive);
    res.result(http::status::ok);

    set_standard_headers(res);
    res.set(http::field::content_type, "text/plain");
    res.body() = "OK";
    res.prepare_payload();
  }


};

} // namespace server::models
