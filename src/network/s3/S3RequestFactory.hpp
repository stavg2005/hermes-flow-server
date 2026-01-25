#include <boost/beast/http.hpp>

#include "Config.hpp"
#include "Types.hpp"
#include "awssigv4.h"
#include "boost/beast/http/empty_body.hpp"

namespace hermes::net::s3 {
class S3RequestFactory {
public:
    static std::tm get_safe_gmtime(std::time_t timer);

    static boost::beast::http::request<boost::beast::http::empty_body> create_signed_get_request(
        const config::S3Config& cfg,
        boost::beast::http::verb method,
        std::string file_key);
};
}
