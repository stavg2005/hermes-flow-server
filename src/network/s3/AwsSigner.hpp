#pragma once
#include <boost/beast/http/verb.hpp>
#include <string>

#include "Config.hpp"


namespace hermes::net::s3 {

struct SignedRequestHeaders {
  std::string authorization;
  std::string date;
  std::string content_sha256;
};

/**
 * @brief Encapsulates AWS Signature V4 logic.
 * Handles canonicalization, date formatting, and signature generation.
 */
class AwsSigner {
 public:
  /**
   * @brief Generates the required AWS V4 headers for a request.
   * @param cfg S3 Configuration (Access Key, Secret, Region, Service).
   * @param method HTTP Method (GET, PUT, etc.).
   * @param host The calculated Host header (e.g., "mybucket.s3.amazonaws.com").
   * @param uri_path The absolute path (e.g., "/my-file.wav").
   * @return A struct containing the computed headers.
   */
  static SignedRequestHeaders sign(const config::S3Config& cfg,
                                   boost::beast::http::verb method,
                                   const std::string& host,
                                   const std::string& uri_path);

 private:
  static std::tm get_safe_gmtime(std::time_t timer);
};

}  // namespace hermes::net::s3
