#include <boost/beast/http.hpp>
#include <iomanip>
#include <sstream>

#include "S3Client.hpp"
#include "awssigv4.h"
#include "boost/beast/http/empty_body.hpp"
#include "types.hpp"

namespace S3RequestFactory {

// Helper for Thread-Safe Time
std::tm get_safe_gmtime(std::time_t timer) {
    std::tm tm_snapshot;
#if defined(_WIN32)
    gmtime_s(&tm_snapshot, &timer);
#else
    gmtime_r(&timer, &tm_snapshot);
#endif
    return tm_snapshot;
}

// THE FACADE FUNCTION
http::request<http::empty_body> create_signed_GET_request(const S3Config& cfg, http::verb method,
                                                          std::string file_key) {
    std::time_t now = std::time(nullptr);
    std::tm timeinfo = get_safe_gmtime(now);

    // 1. Determine Host & URI (MinIO vs AWS Logic)
    bool is_aws = (cfg.host.find("amazonaws.com") != std::string::npos);
    std::string full_host = cfg.host;
    std::string canonical_uri;
    /* --------------------------------------------------------------------------
     * S3 Addressing Mode Selection
     * --------------------------------------------------------------------------
     * 1. AWS S3 (Virtual-Hosted-Style):
     * Format: https://bucket-name.s3.region.amazonaws.com/key
     * Required by modern AWS regions for DNS compatibility.
     *
     * 2. MinIO / Local (Path-Style):
     * Format: https://localhost:9000/bucket-name/key
     * Used when DNS subdomains are not feasible (e.g., local docker IP).
     */
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

    // 2. Initialize Signer
    aws_sigv4::Signature signer(cfg.service, full_host, cfg.region, cfg.secret_key, cfg.access_key,
                                now);

    // since its a get request the request dosent have a body so it would be a hash of an empty
    // string we hardcode it to avoid redundent calculations
    std::string payload_hash = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

    // 4. Prepare Headers for Signature
    // We manually format the date here to ensure consistency with the signer's internal time
    char amzdate[20];
    std::strftime(amzdate, sizeof(amzdate), "%Y%m%dT%H%M%SZ", &timeinfo);

    // to comply with the library format we do it like this
    std::map<std::string, std::vector<std::string>> headers;
    headers["host"] = {full_host};
    headers["x-amz-content-sha256"] = {payload_hash};
    headers["x-amz-date"] = {amzdate};

    std::string canonical_req =
        signer.createCanonicalRequest("GET", canonical_uri, "", headers, "");
    std::string string_to_sign = signer.createStringToSign(canonical_req);
    std::string signature = signer.createSignature(string_to_sign);
    std::string auth_header = signer.createAuthorizationHeader(signature);

    // 6. Build Boost.Beast Request
    http::request<http::empty_body> req{method, canonical_uri, 11};

    // Set Standard Headers
    req.set(http::field::host, full_host);
    req.set(http::field::authorization, auth_header);
    req.set("x-amz-date", amzdate);
    req.set("x-amz-content-sha256", payload_hash);

    return req;
}
}  // namespace S3RequestFactory
