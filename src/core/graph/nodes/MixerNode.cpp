#include "MixerNode.hpp"

#include <spdlog/spdlog.h>

#include <memory>
#include <ranges>
#include <typeinfo>

#include "AudioMath.hpp"
#include "PcmCast.hpp"
#include "Config.hpp"
#include "Node.hpp"
#include "Types.hpp"
#include "nodes/FileInputNode.hpp"

using namespace hermes::config;
namespace hermes::audio {

MixerNode::MixerNode(Node* t) : Node(t) { kind_ = NodeKind::Mixer; }

// Removed as_audio

void MixerNode::set_max_frames() {
  int max = 0;
  for (auto* source : inputs_) {
    max = std::max(max, source->get_total_frames());
  }
  total_frames_ = max;
  spdlog::info("Mixer total frames set to: {}", max);
}

void MixerNode::add_input(FileInputNode* node) {
  inputs_.push_back(node);
}

std::expected<void, NodeError> MixerNode::connect_input(Node* source) {
  if (source->kind() == NodeKind::Mixer) {
    return error(config::NodeErrorCode::FormatError,
                 "Mixer cannot connect to another Mixer.");
  }

  if (source->kind() == NodeKind::Clients) {
    return error(config::NodeErrorCode::FormatError,
                 "Mixer cannot accept ClientsNode as input.");
  }

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
    inputs_.push_back(source);
  }

  wire_standard(source);

  return {};
}

void MixerNode::set_in_loop(bool val){
  is_in_loop_ =val;
  for(auto* input:inputs_){
    input->set_in_loop(val);
  }
}

std::expected<void, NodeError> MixerNode::close() {
  for (auto* source : inputs_) {
    auto result = source->close();
    if (!result) {
      return std::unexpected<NodeError>(result.error());
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

  for (auto* source : inputs_) {
    auto result = source->process_frame(temp_input_buffer_);
    if (!result) {
      // Handle non-critical errors (Underrun, EOS) by skipping
      if (result.error().code == NodeErrorCode::Critical)
        return std::unexpected(result.error());
      continue;
    }

    has_active_inputs = true;

    auto input_samples = pcm::as_samples(
        std::span<const uint8_t>(temp_input_buffer_));

    AudioMath::sum_buffers(accumulator_, input_samples);
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
