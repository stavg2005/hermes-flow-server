#pragma once
#include "Node.hpp"

namespace hermes::audio {
/**
 * @brief Holds configuration options for FileInput nodes like gain adjustment.
 */
struct FileOptionsNode : Node {
  double gain{1.0};
  explicit FileOptionsNode(Node* t = nullptr);
};
}  // namespace hermes::audio
