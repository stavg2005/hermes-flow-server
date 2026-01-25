#include "ClientsNode.hpp"

namespace hermes::audio {

ClientsNode::ClientsNode(Node* t) : Node(t) { kind_ = NodeKind::Clients; }

void ClientsNode::add_client(std::string ip, uint16_t port) {
  clients.emplace(std::move(ip), port);
}

std::expected<void, config::NodeError> ClientsNode::connect_input(
    std::shared_ptr<Node> source) {
  // RULE: "Cant be connected to"
  return error(config::NodeErrorCode::FormatError,
               "ClientsNode cannot accept incoming connections.");
}
}  // namespace hermes::audio
