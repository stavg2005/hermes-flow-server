#pragma once
#include <chrono>
#include <cstdint>
#include <expected>
#include <string>

#include "Types.hpp"
namespace hermes::config {

// Assumptions: 8kHz Sampling Rate, 16-bit Mono PCM
static constexpr size_t FRAME_DURATION = 20;       // Duration in ms
static constexpr size_t SAMPLES_PER_FRAME = 160;   // 8000 Hz * 0.020 s
static constexpr size_t BYTES_PER_SAMPLE = 2;      // 16-bit
static constexpr size_t FRAME_SIZE_BYTES =
    SAMPLES_PER_FRAME * BYTES_PER_SAMPLE;          // 320 bytes per frame
static constexpr size_t WAV_HEADER_SIZE = 44;
static constexpr int MS = 20;
static constexpr size_t PAYLOAD_TYPE = 8;          // PCMA (G.711 A-law)
static constexpr size_t BUFFER_SIZE = 1024UZ * 128UZ;
static constexpr size_t RTP_HEADER_SIZE = 12;

// Audio Soft-Clipping Limits (to avoid hardware distortion near max/min)
static constexpr float MAX_INT16 = 32767.0F;
static constexpr size_t CLIP_LIMIT_POSITIVE = 30000;
static constexpr size_t CLIP_LIMIT_NEGATIVE = 30000;

struct ServerConfig {
  std::string address = "127.0.0.1";
  uint16_t port = 8080;  // NOLINT
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
 */
std::expected<AppConfig, ErrorInfo> load_config(
    const std::string& path = "config.toml");
};  // namespace hermes::config
