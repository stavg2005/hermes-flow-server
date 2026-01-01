#include "NodeRegistry.hpp"

#include <spdlog/spdlog.h>

#include <charconv>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>

#include "NodeFactory.hpp"
#include "Nodes.hpp"
#include "boost/json/object.hpp"
#include "types.hpp"

// =========================================================
//  Helpers
// =========================================================

namespace {

/**
 * @brief Extract a value from a JSON object, or return a default if missing/null.
 */
template <class T>
T get_or(const json::object ojson, const char* key, T default_val) {
    if (!ojson.contains(key) || ojson.at(key).is_null()) return default_val;
    try {
        return json::value_to<T>(ojson.at(key));
    } catch (...) {
        return default_val;
    }
}

/**
 * @brief Extract a mandatory value from a JSON object. Throws if missing.
 */
template <class T>
T require(const json::object& ojson, const char* key) {
    if (!ojson.contains(key)) {
        throw std::runtime_error(std::string("Missing required config key: ") + key);
    }
    return json::value_to<T>(ojson.at(key));
}

// =========================================================
//  Factory Creation Functions
// =========================================================
// These functions match the signature required by NodeFactory.
// They take the IO context and a JSON object, and return a Node.

std::shared_ptr<Node> CreateFileInput(boost::asio::io_context& io, const json::object& data) {
    std::string name = require<std::string>(data, "fileName");
    // TODO: Make the download path configurable via config.toml
    std::string path = "downloads/" + name;

    return std::make_shared<FileInputNode>(io, name, path);
}

std::shared_ptr<Node> CreateMixer(boost::asio::io_context&, const json::object&) {
    return std::make_shared<MixerNode>();
}

std::shared_ptr<Node> CreateDelay(boost::asio::io_context&, const json::object& data) {
    auto node = std::make_shared<DelayNode>();
    // Convert seconds (JSON) to milliseconds (Internal)
    node->delay_ms = require<float>(data, "delay") * 1000;
    node->total_frames = static_cast<int>(node->delay_ms / FRAME_DURATION);
    return node;
}

std::shared_ptr<Node> CreateFileOptions(boost::asio::io_context&, const json::object& data) {
    auto node = std::make_shared<FileOptionsNode>();
    node->gain = require<double>(data, "gain");
    return node;
}

std::shared_ptr<Node> CreateClients(boost::asio::io_context&, const json::object& data) {
    auto node = std::make_unique<ClientsNode>();

    if (data.contains("clients")) {
        for (const auto& v : require<json::array>(data, "clients")) {
            const auto& client_ojson = v.as_object();
            std::string ip = require<std::string>(client_ojson, "ip");

            // Parse port (int or string).
            uint16_t port_val = 0;
            const auto& json_port = client_ojson.at("port");

            if (json_port.is_int64()) {
                port_val = static_cast<uint16_t>(json_port.as_int64());
            } else if (json_port.is_string()) {
                std::string_view p_str = json_port.as_string();
                auto [ptr, ec] =
                    std::from_chars(p_str.data(), p_str.data() + p_str.size(), port_val);
                if (ec != std::errc()) {
                    spdlog::error("Invalid port format for client IP {}: {}", ip, p_str);
                    continue;
                }
            }

            node->AddClient(ip, port_val);
        }
    }
    return node;
}

}  // namespace

// =========================================================
//  Public Registration API
// =========================================================

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
