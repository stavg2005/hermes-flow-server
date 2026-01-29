#include "Node.hpp"
namespace hermes::audio {
Node::Node(Node* t) : target_(t) {}

IAudioProcessor* Node::as_audio() { return nullptr; }

void Node::wire_standard(Node* source) { source->set_next(this); }
std::expected<void, config::NodeError> Node::connect_input(Node* source) {
  return error(config::NodeErrorCode::FormatError,
               "Node type {} does not accept inputs.", id_);
}
}  // namespace hermes::audio
