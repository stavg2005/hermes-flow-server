#pragma once
#include <chrono>
#include <cstdint>
#include <expected>
#include <string>

#include "Types.hpp"
namespace hermes::config {

// Assumptions: 8kHz Sampling Rate, 16-bit Mono PCM
inline constexpr int SAMPLE_RATE = 8000;
inline constexpr int CHANNELS = 1;
inline constexpr int WINDOW_MS = 50;

// חישוב גודל ה-Buffer הנדרש בדגימות
inline constexpr int MAX_DELAY_SAMPLES = (SAMPLE_RATE * WINDOW_MS) / 1000;
inline constexpr int DELAY_BUFFER_SIZE = MAX_DELAY_SAMPLES * 2;
inline constexpr size_t FRAME_DURATION = 20;      // Duration in ms
inline constexpr size_t SAMPLES_PER_FRAME = 160;  // 8000 Hz * 0.020 s
inline constexpr size_t BYTES_PER_SAMPLE = 2;     // 16-bit
inline constexpr size_t FRAME_SIZE_BYTES =
    SAMPLES_PER_FRAME * BYTES_PER_SAMPLE;  // 320 bytes per frame
inline constexpr size_t WAV_HEADER_SIZE = 44;
inline constexpr int MS = 20;
inline constexpr size_t PAYLOAD_TYPE = 8;  // PCMA (G.711 A-law)
inline constexpr size_t BUFFER_SIZE = 1024UZ * 128UZ;
inline constexpr size_t RTP_HEADER_SIZE = 12;

// Audio Soft-Clipping Limits (to avoid hardware distortion near max/min)
inline constexpr float MAX_INT16 = 32767.0F;
inline constexpr size_t CLIP_LIMIT_POSITIVE = 30000;
inline constexpr size_t CLIP_LIMIT_NEGATIVE = 30000;

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

struct JnausConfig {
  std::string address;
  uint16_t port_start;
  uint16_t port_end;
};

struct CryptoConfig {
  std::vector<uint8_t> master_key;
  std::vector<uint8_t> salt;
};

struct AppConfig {
  ServerConfig server;
  S3Config s3;
  JnausConfig janus;
  CryptoConfig crypto;
};

enum class SessionType { Standard, StandartEncrypted, WebRTC };
/**
 * @brief Loads configuration from a TOML file.
 * @param path Path to the .toml file (default: "config.toml")
 * @return Parsed AppConfig object.
 */
std::expected<AppConfig, ErrorInfo> load_config(
    const std::string& path = "config.toml");
};  // namespace hermes::config
