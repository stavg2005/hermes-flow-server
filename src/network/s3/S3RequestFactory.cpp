#include <boost/beast/http.hpp>

#include "Config.hpp"
#include "Types.hpp"
#include "boost/beast/http/empty_body.hpp"
#include "S3RequestFactory.hpp"
#include "AwsSigner.hpp"
using namespace hermes::config;
namespace hermes::net::s3 {

// Helper for Thread-Safe Time
std::tm S3RequestFactory::get_safe_gmtime(std::time_t timer) {
  std::tm tm_snapshot;
#if defined(_WIN32)
  gmtime_s(&tm_snapshot, &timer);
#else
  gmtime_r(&timer, &tm_snapshot);
#endif
  return tm_snapshot;
}

http::request<http::empty_body> S3RequestFactory::create_signed_get_request(
    const S3Config& cfg, http::verb method, std::string file_key) {
 bool is_aws = cfg.host.contains("amazonaws.com");
  std::string full_host = cfg.host;
  std::string canonical_uri;

  if (is_aws) {
    // Virtual-hosted-style: bucket.s3.region.amazonaws.com
    full_host = cfg.bucket + ".s3." + cfg.region + ".amazonaws.com";
    canonical_uri = "/" + file_key;
  } else {
    // Path-style: localhost:9000/bucket/key
    if (!cfg.port.empty() && cfg.port != "80" && cfg.port != "443") {
      full_host += ":" + cfg.port;
    }
    canonical_uri = "/" + cfg.bucket + "/" + file_key;
  }


  auto signed_headers = AwsSigner::Sign(cfg, method, full_host, canonical_uri);


  http::request<http::empty_body> req{method, canonical_uri, 11};

  req.set(http::field::host, full_host);
  req.set(http::field::authorization, signed_headers.authorization);
  req.set("x-amz-date", signed_headers.date);
  req.set("x-amz-content-sha256", signed_headers.content_sha256);

  return req;
}
}  // namespace hermes::net::s3::S3RequestFactory
