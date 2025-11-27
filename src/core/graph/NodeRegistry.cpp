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
    node->delay_ms = require<int>(data, "delay") *1000; //seconds to milliseconds
    node->total_frames = node->delay_ms / FRAME_DURATION;
    return node;
}

std::shared_ptr<Node> CreateClients(boost::asio::io_context&, const bj::object&) {
    return std::make_shared<ClientsNode>();
}

std::shared_ptr<Node> CreateFileOptions(boost::asio::io_context&, const bj::object& data) {
    auto node = std::make_shared<FileOptionsNode>();
    node->gain = get_or<double>(data, "gain", 1.0);
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
