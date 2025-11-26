#pragma once
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <stdexcept>

#include <boost/asio/io_context.hpp>
#include <boost/json.hpp>

#include "Nodes.hpp"

// Define the signature for a function that creates a node
using NodeCreator = std::shared_ptr<Node>(*)(boost::asio::io_context&, const boost::json::object&);

class NodeFactory {
public:
    // Singleton Access
    static NodeFactory& Instance() {
        static NodeFactory instance;
        return instance;
    }

    // Register a new node type
    void Register(const std::string& type, NodeCreator creator) {
        creators_[type] = std::move(creator);
    }

    // Create a node by string type
    std::shared_ptr<Node> Create(const std::string& type, boost::asio::io_context& io, const boost::json::object& data) {
        auto it = creators_.find(type);
        if (it == creators_.end()) {
            throw std::runtime_error("Unknown node type: " + type);
        }
        // Call the registered creator function
        return (it->second)(io, data);
    }

private:
    std::unordered_map<std::string, NodeCreator> creators_;
    NodeFactory() = default;
};
