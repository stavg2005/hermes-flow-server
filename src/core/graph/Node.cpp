#include "Node.hpp"
namespace hermes::audio {
Node::Node(Node* t) : target_(t->shared_from_this()) {}

IAudioProcessor* Node::AsAudio() { return nullptr; }

void Node::WireStandard(std::shared_ptr<Node> source) {
  source->SetNext(this->shared_from_this());
}
std::expected<void, config::NodeError> Node::ConnectInput(
    std::shared_ptr<Node> source) {
  return Error(config::NodeErrorCode::FormatError,
               "Node type {} does not accept inputs.", id_);
}
}  // namespace hermes::audio
