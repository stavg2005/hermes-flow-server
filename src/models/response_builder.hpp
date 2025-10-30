#pragma once
#include "boost/beast/http/message_fwd.hpp"
#include "boost/json.hpp" // IWYU pragma: keep
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <sstream>
#include <string>

namespace server::models {
namespace json = boost::json;
namespace http = boost::beast::http;

class ResponseBuilder {
public:
  static void build_success_response(http::response<http::string_body> &res,
                                    const std::string &filename, 
                                    size_t bytes_received,
                                    size_t bytes_processed, 
                                    unsigned int version,
                                    bool keep_alive = false) {

    res.version(version);
    res.keep_alive(keep_alive);
    res.result(http::status::ok);

    add_common_headers(res);
    res.set(http::field::content_type, "application/json");

    std::stringstream json;
    json << "{"
         << "\"status\":\"success\","
         << "\"message\":\"Audio file uploaded successfully\","
         << "\"filename\":\"" << filename << "\","
         << "\"bytes_received\":" << bytes_received << ","
         << "\"bytes_processed\":" << bytes_processed << "}";

    res.body() = json.str();
    res.set(http::field::content_length, std::to_string(res.body().size()));
    res.prepare_payload();
  }

  static void add_cors_headers(http::response<http::string_body> &res) {
    res.set(http::field::access_control_allow_origin, "*");
    res.set(http::field::access_control_allow_methods,
            "GET, POST, PUT, DELETE, OPTIONS");
    res.set(http::field::access_control_allow_headers,
            "Content-Type, Authorization");
    res.set(http::field::access_control_max_age, "3600");
  }

  static void build_success_response_with_id(http::response<http::string_body> &res,
                                            const std::string &filename,
                                            size_t bytes_received, 
                                            size_t bytes_processed,
                                            uint32_t file_id, 
                                            unsigned version,
                                            bool keep_alive) {

    res = http::response<http::string_body>{http::status::ok, version};
    res.set(http::field::server, "WavBridge/1.0");
    res.set(http::field::content_type, "application/json");
    res.keep_alive(keep_alive);

    // ADD CORS HEADERS
    add_cors_headers(res);

    // Build JSON response
    json::object response_json;
    response_json["status"] = "success";
    response_json["message"] = "File uploaded successfully";
    response_json["filename"] = filename;
    response_json["file_id"] = file_id;
    response_json["bytes_received"] = bytes_received;
    response_json["bytes_processed"] = bytes_processed;

    std::string body = json::serialize(response_json);
    res.body() = body;
    res.prepare_payload();
  }

  static void make_json_response(http::response<http::string_body> &res,
                                http::status status,
                                const boost::json::value &val) {
    try {
      res.result(status);
      res.version(11);
      res.set(http::field::content_type, "application/json");
      res.body() = boost::json::serialize(val);
      res.prepare_payload();
    } catch (const boost::system::system_error &e) {
      build_error_response(res,
          "Failed to serialize JSON response: " + std::string(e.what()),
          res.version(), false, http::status::internal_server_error);
    } catch (const std::exception &e) {
      build_error_response(res,
          "Failed to serialize JSON response: " + std::string(e.what()),
          res.version(), false, http::status::internal_server_error);
    }
  }

  static void build_error_response(http::response<http::string_body> &res,
                                  const std::string &error_message, 
                                  unsigned int version,
                                  bool keep_alive = false,
                                  http::status status = http::status::bad_request) {

    res.version(version);
    res.keep_alive(keep_alive);
    res.result(status);

    add_common_headers(res);
    res.set(http::field::content_type, "application/json");

    std::stringstream json;
    json << "{\"status\":\"error\",\"message\":\"" << error_message << "\"}";

    res.body() = json.str();
    res.set(http::field::content_length, std::to_string(res.body().size()));
    res.prepare_payload();
  }

  static void build_options_response(http::response<http::string_body> &res,
                                    unsigned int version, 
                                    bool keep_alive = true) {

    res.version(version);
    res.keep_alive(keep_alive);
    res.result(http::status::ok);

    add_common_headers(res);
    res.set(http::field::content_type, "text/plain");
    res.body() = "OK";
    res.prepare_payload();
  }

private:
  static void add_common_headers(http::response<http::string_body> &res) {
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, "application/json"); 
    res.set(http::field::access_control_allow_origin, "*");
    res.set(http::field::access_control_allow_methods,
            "GET, POST, PUT, DELETE, OPTIONS");
    res.set(http::field::access_control_allow_headers,
            "Content-Type, Authorization");
  }
};

} // namespace server::models