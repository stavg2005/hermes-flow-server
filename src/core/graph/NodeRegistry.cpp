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

std::expected<std::shared_ptr<Node>, ErrorInfo> create_file_input(
    boost::asio::io_context& io, const json::object& data) {
  auto name_res = require<std::string>(data, "fileName");
  if (!name_res) return std::unexpected(name_res.error());

  std::string name = *name_res;
  std::string path = "downloads/" + name;

  // TODO: Add path traversal check here if needed (AppError::FileSystemError)

  return std::make_unique<FileInputNode>(io, name, path);
}

std::expected<std::shared_ptr<Node>, ErrorInfo> create_mixer(
    boost::asio::io_context&, const json::object&) {
  return std::make_unique<MixerNode>();
}

std::expected<std::shared_ptr<Node>, ErrorInfo> create_delay(
    boost::asio::io_context&, const json::object& data) {
  return require<float>(data, "delay")
      .and_then([](float delay_sec) -> std::expected<float, ErrorInfo> {
        if (delay_sec < 0) {
          return std::unexpected(ErrorInfo::From(AppError::ParseError,
                                                 "Delay cannot be negative"));
        }
        return delay_sec;
      })

      .transform([](float delay_sec) {
        return std::make_unique<DelayNode>(delay_sec);
      });
}

std::expected<std::shared_ptr<Node>, ErrorInfo> create_file_options(
    boost::asio::io_context&, const json::object& data) {
  auto node = std::make_unique<FileOptionsNode>();

  auto gain_res = require<double>(data, "gain");
  if (!gain_res) return std::unexpected(gain_res.error());

  node->gain = *gain_res;
  return node;
}

std::expected<std::shared_ptr<Node>, ErrorInfo> create_clients(
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

      node->add_client(ip, port_val);
    }
  }
  return node;
}

/**
 * @brief registers default notes with their creator function
 */
void register_builtin_nodes() {
  auto& factory = NodeFactory::instance();

  // Map string keys (from JSON) to C++ creation functions
  factory.register_creator("fileInput", create_file_input);
  factory.register_creator("mixer", create_mixer);
  factory.register_creator("delay", create_delay);
  factory.register_creator("clients", create_clients);
  factory.register_creator("fileOptions", create_file_options);

  spdlog::debug("Registered built-in node types.");
}
}  // namespace hermes::audio
