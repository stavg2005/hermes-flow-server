#pragma once
#include <chrono>
#include <cstdint>
#include <expected>
#include <string>

#include "Types.hpp"
namespace hermes::config {

// Assumptions: 8kHz Sampling Rate, 16-bit Mono PCM
static constexpr int SAMPLE_RATE = 8000;
static constexpr int CHANNELS = 1;
static constexpr int WINDOW_MS = 50;

// חישוב גודל ה-Buffer הנדרש בדגימות
static constexpr int MAX_DELAY_SAMPLES = (SAMPLE_RATE * WINDOW_MS) / 1000;
static constexpr int DELAY_BUFFER_SIZE = MAX_DELAY_SAMPLES * 2;
static constexpr size_t FRAME_DURATION = 20;      // Duration in ms
static constexpr size_t SAMPLES_PER_FRAME = 160;  // 8000 Hz * 0.020 s
static constexpr size_t BYTES_PER_SAMPLE = 2;     // 16-bit
static constexpr size_t FRAME_SIZE_BYTES =
    SAMPLES_PER_FRAME * BYTES_PER_SAMPLE;  // 320 bytes per frame
static constexpr size_t WAV_HEADER_SIZE = 44;
static constexpr int MS = 20;
static constexpr size_t PAYLOAD_TYPE = 8;  // PCMA (G.711 A-law)
static constexpr size_t BUFFER_SIZE = 1024UZ * 128UZ;
static constexpr size_t RTP_HEADER_SIZE = 12;
static constexpr std::array<uint8_t, 16> STATIC_RTP_KEY = {
    0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6,
    0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C};
static const std::vector<uint8_t> STATIC_SALT = {
    0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6, 0x07, 0x18,
    0x29, 0x3A, 0x4B, 0x5C, 0x6D, 0x7E, 0x8F, 0x90};

static const std::vector<uint8_t> STATIC_MASTER_KEY = {
        0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6,
        0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C
    };
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

struct JnausConfig {
  std::string address;
  uint16_t port_start;
  uint16_t port_end;
};

struct AppConfig {
  ServerConfig server;
  S3Config s3;
  JnausConfig janus;
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
