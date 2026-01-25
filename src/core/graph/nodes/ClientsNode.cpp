#include "ClientsNode.hpp"

namespace hermes::audio {

ClientsNode::ClientsNode(Node* t) : Node(t) { kind_ = NodeKind::Clients; }

void ClientsNode::AddClient(std::string ip, uint16_t port) {
  clients.emplace(std::move(ip), port);
}

std::expected<void, config::NodeError> ClientsNode::ConnectInput(
    std::shared_ptr<Node> source) {
  // RULE: "Cant be connected to"
  return Error(config::NodeErrorCode::FormatError,
               "ClientsNode cannot accept incoming connections.");
}
}  // namespace hermes::audio
