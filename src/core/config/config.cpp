#include "config.hpp"

#include <spdlog/spdlog.h>

#include <filesystem>
#include <stdexcept>
#include <toml++/toml.hpp>


AppConfig LoadConfig(const std::string& path) {
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
        throw std::runtime_error("Config parse error");
    }

    // 1. Server Settings
    if (auto server = tbl["server"]) {
        config.server.address = server["address"].value_or("0.0.0.0");
        config.server.port = server["port"].value_or<uint16_t>(8080);  // NOLINT
        config.server.threads = server["threads"].value_or<unsigned int>(1);
    }

    // 2. S3 Settings
    if (auto s3 = tbl["s3"]) {
        config.s3.host = s3["host"].value_or("localhost");
        config.s3.port = s3["port"].value_or("9000");
        config.s3.bucket = s3["bucket"].value_or("audio-files");
        config.s3.access_key = s3["access_key"].value_or("minioadmin");
        config.s3.secret_key = s3["secret_key"].value_or("minioadmin123");
        config.s3.region = s3["region"].value_or("us-east-1");
        config.s3.service = s3["service"].value_or("s3");
    }

    spdlog::info("Loaded configuration from {}", path);
    return config;
}
