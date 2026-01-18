#include "Node.hpp"
namespace hermes::audio {
Node::Node(Node* t) : target_(t) {}

IAudioProcessor* Node::AsAudio() { return nullptr; }

}  // namespace hermes::audio
