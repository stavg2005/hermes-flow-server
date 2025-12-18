#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <unordered_map>

#include "NodeFactory.hpp"
#include "Nodes.hpp"

namespace bj = boost::json;

namespace {

// --- JSON Helpers ---
template <class T>
T get_or(const bj::object& obj, const char* key, T default_val) {
    if (!obj.contains(key) || obj.at(key).is_null()) return default_val;
    try {
        return bj::value_to<T>(obj.at(key));
    } catch (...) {
        return default_val;
    }
}

template <class T>
T require(const bj::object& obj, const char* key) {
    if (!obj.contains(key)) throw std::runtime_error(std::string("Missing key: ") + key);
    return bj::value_to<T>(obj.at(key));
}

// --- Creation Functions ---

std::shared_ptr<Node> CreateFileInput(boost::asio::io_context& io, const bj::object& data) {
    std::string name = require<std::string>(data, "fileName");
    std::string path = "downloads/" + name;

    // Note: Assuming FileInputNode constructor handles 'kind = NodeKind::FileInput'
    return std::make_shared<FileInputNode>(io, name, path);
}

std::shared_ptr<Node> CreateMixer(boost::asio::io_context&, const bj::object&) {
    return std::make_shared<MixerNode>();
}

std::shared_ptr<Node> CreateDelay(boost::asio::io_context&, const bj::object& data) {
    auto node = std::make_shared<DelayNode>();
    node->delay_ms = require<float>(data, "delay") * 1000;  // seconds to milliseconds
    node->total_frames = node->delay_ms / FRAME_DURATION;
    return node;
}

std::shared_ptr<Node> CreateClients(boost::asio::io_context&, const bj::object& data) {
    // 1. Create the empty node first
    auto node = std::make_unique<ClientsNode>();

    // 2. Parse the array
    if (data.contains("clients")) {
        for (const auto& v : require<bj::array>(data, "clients")) {
            const auto& client_obj = v.as_object();

            std::string ip = require<std::string>(client_obj, "ip");

            uint16_t port_val = 0;
            const auto& json_port = client_obj.at("port");

            if (json_port.is_int64()) {
                port_val = static_cast<uint16_t>(json_port.as_int64());
            } else if (json_port.is_string()) {
                std::string_view p_str = json_port.as_string();
                auto [ptr, ec] =
                    std::from_chars(p_str.data(), p_str.data() + p_str.size(), port_val);

                if (ec != std::errc()) {
                    spdlog::error("Failed to parse port for IP {}: {}", ip, p_str);
                    continue;
                }
            }

            spdlog::info("Configuring client: {}:{}", ip, port_val);
            node->AddClient(ip, port_val);
        }
    }

    return node;
}
std::shared_ptr<Node> CreateFileOptions(boost::asio::io_context&, const bj::object& data) {
    auto node = std::make_shared<FileOptionsNode>();
    node->gain = require<double>(data, "gain");
 
    return node;
}

}  // namespace

void RegisterBuiltinNodes() {
    auto& factory = NodeFactory::Instance();

    // Register simple function pointers
    factory.Register("fileInput", CreateFileInput);
    factory.Register("mixer", CreateMixer);
    factory.Register("delay", CreateDelay);
    factory.Register("clients", CreateClients);
    factory.Register("fileOptions", CreateFileOptions);
}
