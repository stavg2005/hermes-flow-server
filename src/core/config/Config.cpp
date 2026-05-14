#include "Config.hpp"

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <filesystem>
#include <toml++/toml.hpp>

namespace hermes::config {
constexpr uint16_t DEFAULT_SERVER_PORT = 8080;
constexpr uint16_t DEFAULT_JANUS_PORT_START = 10000;
constexpr uint16_t DEFAULT_JANUS_PORT_END = 10200;

static std::vector<uint8_t> hex_to_bytes(const std::string& hex) {
  if (hex.length() % 2 != 0) {
    throw std::invalid_argument("Hex string must have an even length");
  }
  std::vector<uint8_t> bytes;
  bytes.reserve(hex.length() / 2);
  for (size_t i = 0; i < hex.length(); i += 2) {
    std::string byteString = hex.substr(i, 2);
    uint8_t byte =
        static_cast<uint8_t>(strtol(byteString.c_str(), nullptr, 16));
    bytes.push_back(byte);
  }
  return bytes;
}

template <typename T>
std::expected<T, std::string> require_key(
    const toml::node_view<toml::node>& node, const char* key) {
  auto val = node[key];
  if (!val) {
    return std::unexpected(std::string("Missing required config key: ") + key);
  }

  return val.value<T>().value();
}

std::expected<AppConfig, ErrorInfo> load_config(const std::string& path) {
  AppConfig config;

  if (!std::filesystem::exists(path)) {
    spdlog::warn("Config file '{}' not found. Using defaults.", path);
    return config;
  }

  toml::table tbl;
  try {
    tbl = toml::parse_file(path);
  } catch (const toml::parse_error& err) {
    spdlog::critical("Failed to parse config file: {}", err.description());
    return std::unexpected(
        ErrorInfo::From(AppError::ConfigError, std::string(err.description())));
  }

  // Server Settings
  if (auto server = tbl["server"]) {
    config.server.address = server["address"].value_or("0.0.0.0");
    config.server.port =
        server["port"].value_or<uint16_t>(uint16_t{DEFAULT_SERVER_PORT});
    config.server.threads = server["threads"].value_or<unsigned int>(1);
  }

  // S3 Settings
  if (auto s3 = tbl["s3"]) {
    config.s3.host = s3["host"].value_or("localhost");
    config.s3.port = s3["port"].value_or("9000");
    config.s3.bucket = s3["bucket"].value_or("audio-files");
    config.s3.region = s3["region"].value_or("us-east-1");
    config.s3.service = s3["service"].value_or("s3");

    if (const char* env_ak = std::getenv("S3_ACCESS_KEY")) {
      config.s3.access_key = env_ak;
    } else {
      auto ak_res = require_key<std::string>(s3, "access_key");
      if (!ak_res) {
        return std::unexpected(
            ErrorInfo::From(AppError::ConfigError, "Missing S3 access_key"));
      }
      config.s3.access_key = *ak_res;
    }

    if (const char* env_sk = std::getenv("S3_SECRET_KEY")) {
      config.s3.secret_key = env_sk;
    } else {
      auto sk_res = require_key<std::string>(s3, "secret_key");
      if (!sk_res) {
        return std::unexpected(
            ErrorInfo::From(AppError::ConfigError, "Missing S3 secret_key"));
      }
      config.s3.secret_key = *sk_res;
    }
  }

  if (auto janus = tbl["janus"]) {
    config.janus.address = janus["address"].value_or("127.0.0.1");
    config.janus.port_start = janus["port_start"].value_or<uint16_t>(
        uint16_t{DEFAULT_JANUS_PORT_START});
    config.janus.port_end =
        janus["port_end"].value_or<uint16_t>(uint16_t{DEFAULT_JANUS_PORT_END});
  }

  if (auto crypto_node = tbl["crypto"]) {
    std::string key_hex = crypto_node["master_key"].value_or("");
    std::string salt_hex = crypto_node["salt"].value_or("");

    if (key_hex.empty() || salt_hex.empty()) {
      throw std::runtime_error(
          "Crypto master_key and salt must be provided in config.toml");
    }

    config.crypto.master_key = hex_to_bytes(key_hex);
    config.crypto.salt = hex_to_bytes(salt_hex);
  } else {
    throw std::runtime_error("Missing [crypto] section in config.toml");
  }

  spdlog::info("Loaded configuration from {}", path);
  return config;
}
}  // namespace hermes::config
