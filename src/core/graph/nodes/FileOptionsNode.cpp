#include "FileOptionsNode.hpp"

#include "Node.hpp"

namespace hermes::audio {

FileOptionsNode::FileOptionsNode(Node* t) : Node(t) {
  kind_ = NodeKind::FileOptions;
}
}  // namespace hermes::audio
