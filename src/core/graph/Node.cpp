#include "Node.hpp"
namespace hermes::audio {
Node::Node(Node* t) : target_(t->shared_from_this()) {}

IAudioProcessor* Node::as_audio() { return nullptr; }

void Node::wire_standard(std::shared_ptr<Node> source) {
  source->set_next(this->shared_from_this());
}
std::expected<void, config::NodeError> Node::connect_input(
    std::shared_ptr<Node> source) {
  return error(config::NodeErrorCode::FormatError,
               "Node type {} does not accept inputs.", id_);
}
}  // namespace hermes::audio
