#include "AwsSigner.hpp"

#include <ctime>
#include <map>
#include <vector>

#include "awssigv4.h"


namespace hermes::net::s3 {

std::tm AwsSigner::get_safe_gmtime(std::time_t timer) {
  std::tm tm_snapshot;
#if defined(_WIN32)
  gmtime_s(&tm_snapshot, &timer);
#else
  gmtime_r(&timer, &tm_snapshot);
#endif
  return tm_snapshot;
}

SignedRequestHeaders AwsSigner::sign(const config::S3Config& cfg,
                                     boost::beast::http::verb method,
                                     const std::string& host, // NOLINT(bugprone-easily-swappable-parameters)
                                     const std::string& uri_path) {
  std::time_t now = std::time(nullptr);
  std::tm timeinfo = get_safe_gmtime(now);

  aws_sigv4::Signature signer(cfg.service, host, cfg.region, cfg.secret_key,
                              cfg.access_key, now);

  // Empty string hash to avoid redundent calculation
  std::string payload_hash =
      "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

  constexpr size_t AWS_DATE_BUFFER_SIZE = 20;
  char amzdate[AWS_DATE_BUFFER_SIZE];
  std::strftime(amzdate, sizeof(amzdate), "%Y%m%dT%H%M%SZ", &timeinfo);

  // create Headers Map for Canonicalization
  std::map<std::string, std::vector<std::string>> headers;
  headers["host"] = {host};
  headers["x-amz-content-sha256"] = {payload_hash};
  headers["x-amz-date"] = {amzdate};

  std::string method_str(boost::beast::http::to_string(method));

  std::string canonical_req = signer.createCanonicalRequest(
      method_str, uri_path,
      "",  // Query string (empty)
      headers,
      ""  // Payload (handled via hash above for V4 usually, or passed empty)
  );

  std::string string_to_sign = signer.createStringToSign(canonical_req);
  std::string signature = signer.createSignature(string_to_sign);
  std::string auth_header = signer.createAuthorizationHeader(signature);

  return SignedRequestHeaders{.authorization = auth_header,
                              .date = std::string(amzdate),
                              .content_sha256 = payload_hash};
}

}  // namespace hermes::net::s3
