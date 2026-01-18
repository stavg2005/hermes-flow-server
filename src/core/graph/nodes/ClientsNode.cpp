#include "ClientsNode.hpp"

namespace hermes::audio {

ClientsNode::ClientsNode(Node* t) : Node(t) { kind_ = NodeKind::Clients; }

void ClientsNode::AddClient(std::string ip, uint16_t port) {
  clients.emplace(std::move(ip), port);
}
}  // namespace hermes::audio
