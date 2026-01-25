#include "DelayNode.hpp"

#include <cstddef>

#include "Config.hpp"

using namespace hermes::config;
namespace hermes::audio {
DelayNode::DelayNode(Node* t) : Node(t) { kind_ = NodeKind::Delay; }
DelayNode::DelayNode(size_t ms_delay) {
  delay_ms_ = static_cast<float>(ms_delay) * 1000.0f;
  total_frames_ = static_cast<int>(delay_ms_ / FRAME_DURATION);
}
IAudioProcessor* DelayNode::as_audio() { return this; }

std::expected<void, NodeError> DelayNode::close() {
  in_buffer_processed_frames_ = 0;
  processed_frames_ = 0;
  return {};
}

std::expected<void, NodeError> DelayNode::process_frame(
    std::span<uint8_t> frame_buffer) {
  std::fill(frame_buffer.begin(), frame_buffer.end(), 0);
  in_buffer_processed_frames_++;
  processed_frames_++;
  return {};
}

std::expected<void, config::NodeError> DelayNode::connect_input(
    std::shared_ptr<Node> source) {
  // Rule: "Besides Clients"
  if (source->kind() == NodeKind::Clients) {
    return error(config::NodeErrorCode::FormatError,
                 "DelayNode cannot accept ClientsNode.");
  }

  // Allow everything else (Mixer, FileInput, Delay, etc.)
  wire_standard(source);
  return {};
}
}  // namespace hermes::audio
