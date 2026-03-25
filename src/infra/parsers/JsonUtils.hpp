#pragma once
#include <expected>
#include <string>

#include "Types.hpp"
#include "boost/json/object.hpp"

namespace hermes::infra {

/**
 * @brief Extracts a mandatory typed value from a JSON object.
 *
 * Returns the value on success, or an ErrorInfo on missing key or type
 * mismatch. Used consistently across all JSON parsers in the codebase.
 *
 * @tparam T  The target C++ type (must be supported by boost::json::value_to)
 * @param obj The JSON object to query
 * @param key The required key name
 * @return The extracted value, or an ErrorInfo describing the failure
 */
template <class T>
std::expected<T, hermes::config::ErrorInfo> require_json(
    const boost::json::object& obj, const char* key) {
  if (!obj.contains(key)) {
    return std::unexpected(hermes::config::ErrorInfo::From(
        hermes::config::AppError::ParseError,
        std::string("Missing required key: ") + key));
  }
  try {
    return boost::json::value_to<T>(obj.at(key));
  } catch (const std::exception& e) {
    return std::unexpected(hermes::config::ErrorInfo::From(
        hermes::config::AppError::ParseError,
        std::string("Invalid type for key '") + key + "': " + e.what()));
  }
}

}  // namespace hermes::infra
