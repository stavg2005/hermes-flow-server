#include "DelayNode.hpp"

using namespace hermes::config;
namespace hermes::audio {
DelayNode::DelayNode(Node* t) : Node(t) { kind_ = NodeKind::Delay; }

IAudioProcessor* DelayNode::AsAudio() { return this; }

std::expected<void, NodeError> DelayNode::Close() {
  in_buffer_processed_frames_ = 0;
  processed_frames_ = 0;
  return {};
}

std::expected<void, NodeError> DelayNode::ProcessFrame(
    std::span<uint8_t> frame_buffer) {
  std::fill(frame_buffer.begin(), frame_buffer.end(), 0);
  in_buffer_processed_frames_++;
  processed_frames_++;
  return {};
}
}  // namespace hermes::audio
