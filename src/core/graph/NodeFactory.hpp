#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/json.hpp>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "Nodes.hpp"


// Function signature for creating a node
using NodeCreator = std::shared_ptr<Node> (*)(boost::asio::io_context&, const boost::json::object&);

/**
 * @brief Singleton Factory for instantiating Nodes.
 * * @details
 * Implements the **Factory Method Pattern**. It decouples the JSON parser
 * from the concrete Node classes. The parser only needs to know the string
 * "type" name, and this factory handles the specific constructor logic
 * (which might differ for Mixers vs. Inputs).
 */
class NodeFactory {
   public:
    static NodeFactory& Instance() {
        static NodeFactory instance;
        return instance;
    }

    // Register a new node type with its creator function
    void Register(const std::string& type, NodeCreator creator) { creators_[type] = creator; }

    // Create a node instance
    std::shared_ptr<Node> Create(const std::string& type, boost::asio::io_context& io,
                                 const boost::json::object& data) {
        auto it = creators_.find(type);
        if (it == creators_.end()) {
            throw std::runtime_error("Unknown node type: " + type);
        }
        return (it->second)(io, data);
    }

   private:
    std::unordered_map<std::string, NodeCreator> creators_;
    NodeFactory() = default;
};
