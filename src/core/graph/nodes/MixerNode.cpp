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

IAudioProcessor* MixerNode::AsAudio() { return this; }

void MixerNode::SetMaxFrames() {
  int max = 0;
  for (auto* node : inputs_) {
    max = std::max(max, node->GetTotalFrames());
  }
  total_frames_ = max;
  spdlog::info("Mixer total frames set to: {}", max);
}

void MixerNode::AddInput(FileInputNode* node) { inputs_.push_back(node); }

std::expected<void, NodeError> MixerNode::ConnectInput(
    std::shared_ptr<Node> source) {
  // 1. Check Restrictions
  if (source->Kind() == NodeKind::Mixer) {
    return Error(config::NodeErrorCode::FormatError,
                 "Mixer cannot connect to another Mixer.");
  }

  if (source->Kind() == NodeKind::Clients) {
    return Error(config::NodeErrorCode::FormatError,
                 "Mixer cannot accept ClientsNode as input.");
  }

  // 2. Check Allow List
  bool is_valid = (source->Kind() == NodeKind::Delay ||
                   source->Kind() == NodeKind::FileInput);

  if (!is_valid) {
    return Error(config::NodeErrorCode::FormatError,
                 "Mixer only accepts Delay or FileInput nodes.");
  }

  // inputs vector are for nodes that needs to be mixed for now only fileinput
  // node can be that since delay cant be mixed but can still be connected do
  // delay for garph traversal
  if (source->Kind() == NodeKind::FileInput) {
    auto input = std::dynamic_pointer_cast<FileInputNode>(source);
    inputs_.push_back(input.get());
  }

  // Also do standard graph wiring (so execution flows from source -> mixer)
  WireStandard(source);

  return {};
}

std::expected<void, NodeError> MixerNode::Close() {
  for (auto* input : inputs_) {
    if (auto* audio = input->AsAudio()) {
      auto result = audio->Close();
      if (!result) {
        return std::unexpected<NodeError>(result.error());
      }
    }
  }
  in_buffer_processed_frames_ = 0;
  processed_frames_ = 0;
  return {};
}

std::expected<void, NodeError> MixerNode::ProcessFrame(
    std::span<uint8_t> frame_buffer) {
  accumulator_.fill(0);
  bool has_active_inputs = false;

  for (auto* input_node : inputs_) {
    if (auto* audio_source = dynamic_cast<IAudioProcessor*>(input_node)) {
      auto result = audio_source->ProcessFrame(temp_input_buffer_);
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

      AudioMath::SumBuffers(accumulator_, input_samples);
    }
  }

  if (!has_active_inputs) {
    std::fill(frame_buffer.begin(), frame_buffer.end(), 0);
    return Error(NodeErrorCode::EndOfStream,
                 "Mixer stream ended (no active inputs)");
  }

  AudioMath::CompressAndExport(accumulator_, frame_buffer);

  in_buffer_processed_frames_++;
  processed_frames_++;

  return {};
}
}  // namespace hermes::audio
