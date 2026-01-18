#include "NodeRegistry.hpp"

#include <spdlog/spdlog.h>

#include <charconv>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <system_error>

#include "NodeFactory.hpp"
#include "Nodes.hpp"
#include "Types.hpp"
#include "boost/json/object.hpp"
using namespace hermes::config;
using namespace hermes::audio;
// =========================================================
//  Helpers
// =========================================================

namespace hermes::audio {

/**
 * @brief Extract a mandatory value from a JSON object. Returns expected or
 * ErrorInfo.
 */
template <class T>
std::expected<T, ErrorInfo> require(const json::object& ojson,
                                    const char* key) {
  if (!ojson.contains(key)) {
    return std::unexpected(
        ErrorInfo::From(AppError::ParseError,
                        std::string("Missing required config key: ") + key));
  }
  try {
    return json::value_to<T>(ojson.at(key));
  } catch (const std::exception& e) {
    return std::unexpected(ErrorInfo::From(
        AppError::ParseError,
        std::string("Invalid type for key '") + key + "': " + e.what()));
  }
}

// =========================================================
//  Factory Creation Functions
// =========================================================

std::expected<std::shared_ptr<Node>, ErrorInfo> CreateFileInput(
    boost::asio::io_context& io, const json::object& data) {
  auto name_res = require<std::string>(data, "fileName");
  if (!name_res) return std::unexpected(name_res.error());

  std::string name = *name_res;
  std::string path = "downloads/" + name;

  // TODO: Add path traversal check here if needed (AppError::FileSystemError)

  return std::make_shared<FileInputNode>(io, name, path);
}

std::expected<std::shared_ptr<Node>, ErrorInfo> CreateMixer(
    boost::asio::io_context&, const json::object&) {
  return std::make_shared<MixerNode>();
}

std::expected<std::shared_ptr<Node>, ErrorInfo> CreateDelay(
    boost::asio::io_context&, const json::object& data) {
  auto node = std::make_shared<DelayNode>();

  auto delay_res = require<float>(data, "delay");
  if (!delay_res) return std::unexpected(delay_res.error());

  // Convert seconds (JSON) to milliseconds (Internal)
  node->delay_ms_ = *delay_res * 1000;

  // Safety check for invalid delay
  if (node->delay_ms_ < 0) {
    return std::unexpected(
        ErrorInfo::From(AppError::ParseError, "Delay cannot be negative"));
  }

  node->total_frames_ = static_cast<int>(node->delay_ms_ / FRAME_DURATION);
  return node;
}

std::expected<std::shared_ptr<Node>, ErrorInfo> CreateFileOptions(
    boost::asio::io_context&, const json::object& data) {
  auto node = std::make_shared<FileOptionsNode>();

  auto gain_res = require<double>(data, "gain");
  if (!gain_res) return std::unexpected(gain_res.error());

  node->gain = *gain_res;
  return node;
}

std::expected<std::shared_ptr<Node>, ErrorInfo> CreateClients(
    boost::asio::io_context&, const json::object& data) {
  auto node = std::make_unique<ClientsNode>();

  if (data.contains("clients")) {
    auto arr_res = require<json::array>(data, "clients");
    if (!arr_res) return std::unexpected(arr_res.error());

    for (const auto& v : *arr_res) {
      const auto& client_ojson = v.as_object();

      auto ip_res = require<std::string>(client_ojson, "ip");
      if (!ip_res) return std::unexpected(ip_res.error());
      std::string ip = *ip_res;

      // Parse port (int or string).
      uint16_t port_val = 0;
      if (!client_ojson.contains("port")) {
        return std::unexpected(ErrorInfo::From(
            AppError::ParseError, "Missing port for client " + ip));
      }

      const auto& json_port = client_ojson.at("port");

      if (json_port.is_int64()) {
        port_val = static_cast<uint16_t>(json_port.as_int64());
      } else if (json_port.is_string()) {
        std::string_view p_str = json_port.as_string();
        auto [ptr, ec] = std::from_chars(p_str.data(),
                                         p_str.data() + p_str.size(), port_val);
        if (ec != std::errc()) {
          spdlog::error("Invalid port format for client IP {}: {}", ip, p_str);

          return std::unexpected(
              ErrorInfo::From(AppError::ParseError,
                              "Invalid port format: " + std::string(p_str)));
        }
      } else {
        return std::unexpected(ErrorInfo::From(AppError::ParseError,
                                               "Port must be int or string"));
      }

      node->AddClient(ip, port_val);
    }
  }
  return node;
}



/**
 * @brief registers default notes with their creator function
 */
void RegisterBuiltinNodes() {
  auto& factory = NodeFactory::Instance();

  // Map string keys (from JSON) to C++ creation functions
  factory.Register("fileInput", CreateFileInput);
  factory.Register("mixer", CreateMixer);
  factory.Register("delay", CreateDelay);
  factory.Register("clients", CreateClients);
  factory.Register("fileOptions", CreateFileOptions);

  spdlog::debug("Registered built-in node types.");
}
}  // namespace hermes::audio
