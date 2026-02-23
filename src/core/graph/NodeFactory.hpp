#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/json.hpp>
#include <concepts>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "Node.hpp"
#include "Types.hpp"
namespace hermes::audio {
using NodeCreatorSignature =
    std::expected<std::shared_ptr<Node>, config::ErrorInfo>(
        boost::asio::io_context&, const boost::json::object&);
using NodeCreator = std::function<NodeCreatorSignature>;

template <typename F>
concept IsNodeCreator = requires(F f, boost::asio::io_context& io,
                                 const boost::json::object& data) {
  {
    f(io, data)
  } -> std::convertible_to<
      std::expected<std::shared_ptr<Node>, config::ErrorInfo>>;
};

/**
 * @brief Singleton factory to create Node instances from JSON type strings
 */
class NodeFactory {
 public:
  static NodeFactory& instance() {
    static NodeFactory instance;
    return instance;
  }

  /**
   * @brief Register a new node type.
   * Uses C++20 Concepts to ensure 'Creator' matches the required signature at
   * compile time.
   */
  template <typename Creator>
    requires IsNodeCreator<Creator>
  void register_creator(const std::string& type, Creator&& creator) {
    creators_[type] = std::forward<Creator>(creator);
  }

  std::expected<std::shared_ptr<Node>, config::ErrorInfo> create(
      const std::string& type, boost::asio::io_context& io,
      const boost::json::object& data) {
    auto it = creators_.find(type);
    if (it == creators_.end()) {
      return std::unexpected(config::ErrorInfo::From(
          config::AppError::ParseError, "Unknown node type: " + type));
    }

    return (it->second)(io, data);
  }

 private:
  std::unordered_map<std::string, NodeCreator> creators_;
  NodeFactory() = default;
};
}  // namespace hermes::audio
