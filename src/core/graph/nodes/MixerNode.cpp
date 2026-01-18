#include "MixerNode.hpp"

#include <spdlog/spdlog.h>

#include "Types.hpp"

using namespace hermes::config;
namespace hermes::audio {

MixerNode::MixerNode(Node* t) : Node(t) { kind_ = NodeKind::Mixer; }

IAudioProcessor* MixerNode::AsAudio() { return this; }

void MixerNode::SetMaxFrames() {
  int max = 0;
  for (auto* node : inputs_) {
    max = std::max(max, node->total_frames_);
  }
  total_frames_ = max;
  spdlog::info("Mixer total frames set to: {}", max);
}

void MixerNode::AddInput(FileInputNode* node) { inputs_.push_back(node); }

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
    if (auto* audio_source = input_node->AsAudio()) {
      auto result = audio_source->ProcessFrame(temp_input_buffer_);

      if (!result) {
        NodeError err = result.error();

        if (err.code == NodeErrorCode::FileIOError ||
            err.code == NodeErrorCode::Critical) {
          return std::unexpected(err);
        }
        // EOS or Underrun just means we skip this input for this frame (it's
        // silent) We do NOT set has_active_inputs = true here.
        continue;
      }

      has_active_inputs = true;

      const auto* input_samples =
          reinterpret_cast<const int16_t*>(temp_input_buffer_.data());
      for (size_t i = 0; i < SAMPLES_PER_FRAME; ++i) {
        accumulator_[i] += input_samples[i];
      }
    }
  }

  auto* output_samples = reinterpret_cast<int16_t*>(frame_buffer.data());

  if (!has_active_inputs) {
    std::fill(frame_buffer.begin(), frame_buffer.end(), 0);

    return Error(NodeErrorCode::EndOfStream,
                 "Mixer stream ended (no active inputs)");
  }

  for (size_t i = 0; i < SAMPLES_PER_FRAME; ++i) {
    int32_t raw_sum = accumulator_[i];
    if (raw_sum > CLIP_LIMIT_POSITIVE || raw_sum < CLIP_LIMIT_NEGATIVE) {
      float compressed = std::tanh(static_cast<float>(raw_sum) / MAX_INT16);
      output_samples[i] = static_cast<int16_t>(compressed * MAX_INT16);
    } else {
      output_samples[i] = static_cast<int16_t>(raw_sum);
    }
  }

  in_buffer_processed_frames_++;
  processed_frames_++;

  return {};
}
}  // namespace hermes::audio
