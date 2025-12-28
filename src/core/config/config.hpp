#pragma once
#include <chrono>
#include <string>
#include <cstdint>
static constexpr size_t FRAME_DURATION = 20;
static constexpr size_t SAMPLES_PER_FRAME = 160;
static constexpr size_t BYTES_PER_SAMPLE = 2;
static constexpr size_t FRAME_SIZE_BYTES = SAMPLES_PER_FRAME * BYTES_PER_SAMPLE;  // 16-bit PCM
static constexpr size_t WAV_HEADER_SIZE = 44;
static constexpr int MS = 20;
static constexpr size_t PAYLOAD_TYPE = 8;
static constexpr size_t BUFFER_SIZE = 1024UZ * 128UZ;
static constexpr size_t RTP_HEADER_SIZE = 12;
static constexpr float MAX_INT16 = 32767.0F;
static constexpr size_t CLIP_LIMIT_POSITIVE = 30000;
static constexpr size_t CLIP_LIMIT_NEGATIVE = 30000;




struct ServerConfig {
    std::string address = "127.0.0.1";
    uint16_t port = 8080; //NOLINT
    unsigned int threads = 1;
};
struct S3Config {
    std::string access_key;
    std::string secret_key;
    std::string region;
    std::string host;
    std::string port;
    std::string service;
    std::string bucket;
};

struct AppConfig {
    ServerConfig server;
    S3Config s3;
};

/**
 * @brief Loads configuration from a TOML file.
 * @param path Path to the .toml file (default: "config.toml")
 * @return Parsed AppConfig object.
 * @throws std::runtime_error if file cannot be parsed.
 */
AppConfig LoadConfig(const std::string& path = "config.toml");
