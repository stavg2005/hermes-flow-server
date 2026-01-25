#include "MixerNode.hpp"

#include <spdlog/spdlog.h>

#include <memory>
#include <ranges>

#include "AudioMath.hpp"
#include "Config.hpp"
#include "Node.hpp"
#include "Types.hpp"
#include "nodes/FileInputNode.hpp"

using namespace hermes::config;
namespace hermes::audio {

MixerNode::MixerNode(Node* t) : Node(t) { kind_ = NodeKind::Mixer; }

IAudioProcessor* MixerNode::as_audio() { return this; }

void MixerNode::set_max_frames() {
  int max = 0;
  for (auto* node : inputs_) {
    max = std::max(max, node->get_total_frames());
  }
  total_frames_ = max;
  spdlog::info("Mixer total frames set to: {}", max);
}

void MixerNode::add_input(FileInputNode* node) { inputs_.push_back(node); }

std::expected<void, NodeError> MixerNode::connect_input(
    std::shared_ptr<Node> source) {
  // 1. Check Restrictions
  if (source->kind() == NodeKind::Mixer) {
    return error(config::NodeErrorCode::FormatError,
                 "Mixer cannot connect to another Mixer.");
  }

  if (source->kind() == NodeKind::Clients) {
    return error(config::NodeErrorCode::FormatError,
                 "Mixer cannot accept ClientsNode as input.");
  }

  // 2. Check Allow List
  bool is_valid = (source->kind() == NodeKind::Delay ||
                   source->kind() == NodeKind::FileInput);

  if (!is_valid) {
    return error(config::NodeErrorCode::FormatError,
                 "Mixer only accepts Delay or FileInput nodes.");
  }

  // inputs vector are for nodes that needs to be mixed for now only fileinput
  // node can be that since delay cant be mixed but can still be connected do
  // delay for garph traversal
  if (source->kind() == NodeKind::FileInput) {
    auto input = std::dynamic_pointer_cast<FileInputNode>(source);
    inputs_.push_back(input.get());
  }

  // Also do standard graph wiring (so execution flows from source -> mixer)
  wire_standard(source);

  return {};
}

std::expected<void, NodeError> MixerNode::close() {
  for (auto* input : inputs_) {
    if (auto* audio = input->as_audio()) {
      auto result = audio->close();
      if (!result) {
        return std::unexpected<NodeError>(result.error());
      }
    }
  }
  in_buffer_processed_frames_ = 0;
  processed_frames_ = 0;
  return {};
}

std::expected<void, NodeError> MixerNode::process_frame(
    std::span<uint8_t> frame_buffer) {
  accumulator_.fill(0);
  bool has_active_inputs = false;

  for (auto* input_node : inputs_) {
    if (auto* audio_source = dynamic_cast<IAudioProcessor*>(input_node)) {
      auto result = audio_source->process_frame(temp_input_buffer_);
      if (!result) {
        // Handle non-critical errors (Underrun, EOS) by skipping
        if (result.error().code == NodeErrorCode::Critical)
          return std::unexpected(result.error());
        continue;
      }

      has_active_inputs = true;

      auto input_samples =
          std::span(reinterpret_cast<const int16_t*>(temp_input_buffer_.data()),
                    SAMPLES_PER_FRAME);

      AudioMath::sum_buffers(accumulator_, input_samples);
    }
  }

  if (!has_active_inputs) {
    std::fill(frame_buffer.begin(), frame_buffer.end(), 0);
    return error(NodeErrorCode::EndOfStream,
                 "Mixer stream ended (no active inputs)");
  }

  AudioMath::compress_and_export(accumulator_, frame_buffer);

  in_buffer_processed_frames_++;
  processed_frames_++;

  return {};
}
}  // namespace hermes::audio
