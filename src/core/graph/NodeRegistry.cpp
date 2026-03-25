#include "NodeRegistry.hpp"

#include <spdlog/spdlog.h>

#include <charconv>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <system_error>

#include "JsonUtils.hpp"
#include "NodeFactory.hpp"
#include "Nodes.hpp"
#include "Types.hpp"
#include "boost/json/object.hpp"

using namespace hermes::config;
using namespace hermes::audio;
using namespace hermes::infra;

namespace hermes::audio {

static constexpr std::string_view DOWNLOADS_DIR = "downloads/";

std::expected<uint16_t, ErrorInfo> parse_port(const json::value& json_port,
                                              const std::string& ip) {
  if (json_port.is_int64()) {
    return static_cast<uint16_t>(json_port.as_int64());
  }
  if (json_port.is_string()) {
    uint16_t port_val = 0;
    std::string_view p_str = json_port.as_string();
    auto [ptr, ec] =
        std::from_chars(p_str.data(), p_str.data() + p_str.size(), port_val);
    if (ec != std::errc()) {
      spdlog::error("Invalid port format for client IP {}: {}", ip, p_str);
      return std::unexpected(ErrorInfo::From(
          AppError::ParseError, "Invalid port format: " + std::string(p_str)));
    }
    return port_val;
  }
  return std::unexpected(
      ErrorInfo::From(AppError::ParseError, "Port must be int or string"));
}

std::expected<std::shared_ptr<Node>, ErrorInfo> create_file_input(
    boost::asio::io_context& io, const json::object& data) {
  auto name_res = require_json<std::string>(data, "fileName");
  if (!name_res) return std::unexpected(name_res.error());

  std::string name = *name_res;
  std::string path = std::string(DOWNLOADS_DIR) + name;

  // TODO: Add path traversal check here if needed (AppError::FileSystemError)

  return std::make_unique<FileInputNode>(io, name, path);
}

std::expected<std::shared_ptr<Node>, ErrorInfo> create_mixer(
    boost::asio::io_context&, const json::object&) {
  return std::make_unique<MixerNode>();
}

std::expected<std::shared_ptr<Node>, ErrorInfo> create_delay(
    boost::asio::io_context&, const json::object& data) {
  return require_json<float>(data, "delay")
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

  auto gain_res = require_json<double>(data, "gain");

  if (!gain_res) return std::unexpected(gain_res.error());
  auto pitch_shift_res = require_json<double>(data, "pitch_shift");
  if (!pitch_shift_res) return std::unexpected(pitch_shift_res.error());
  node->gain = *gain_res;
  node->pitch_shift = *pitch_shift_res;
  return node;
}

std::expected<std::shared_ptr<Node>, ErrorInfo> create_clients(
    boost::asio::io_context&, const json::object& data) {
  auto node = std::make_unique<ClientsNode>();

  if (data.contains("clients")) {
    auto arr_res = require_json<json::array>(data, "clients");
    if (!arr_res) return std::unexpected(arr_res.error());

    for (const auto& v : *arr_res) {
      const auto& client_ojson = v.as_object();

      auto ip_res = require_json<std::string>(client_ojson, "ip");
      if (!ip_res) return std::unexpected(ip_res.error());
      std::string ip = *ip_res;

      if (!client_ojson.contains("port")) {
        return std::unexpected(ErrorInfo::From(
            AppError::ParseError, "Missing port for client " + ip));
      }

      auto port_res = parse_port(client_ojson.at("port"), ip);
      if (!port_res) return std::unexpected(port_res.error());

      node->add_client(ip, *port_res);
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
