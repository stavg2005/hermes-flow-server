#pragma once
#include <stdlib.h>

#include "Node.hpp"

namespace hermes::audio {
/**
 * @brief Maintains a list of client endpoints for streaming audio.
 */
struct ClientsNode : Node {
  std::unordered_map<std::string, uint16_t> clients;

  explicit ClientsNode(Node* t = nullptr);

  void AddClient(std::string ip, uint16_t port);

  virtual std::expected<void, config::NodeError> ConnectInput(
      std::shared_ptr<Node> source) override;
};
}  // namespace hermes::audio
