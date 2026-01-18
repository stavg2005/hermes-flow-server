#pragma once
#include "Config.hpp"
#include "FileInputNode.hpp"

namespace hermes::audio {

/**
 * @brief Mixes multiple FileInputNode sources into a single audio stream.
 */
struct MixerNode : Node, IAudioProcessor {
  std::vector<FileInputNode*> inputs_;
  std::array<int32_t, config::SAMPLES_PER_FRAME>
      accumulator_{}; /**< Intermediate mix buffer */
  std::array<uint8_t, config::FRAME_SIZE_BYTES>
      temp_input_buffer_{}; /**< Temp buffer for input frames */

  explicit MixerNode(Node* t = nullptr);

  // Overrides
  IAudioProcessor* AsAudio() override;
  std::expected<void, config::NodeError> ProcessFrame(
      std::span<uint8_t> frame_buffer) override;
  std::expected<void, config::NodeError> Close() override;

  // Specific Methods
  void SetMaxFrames();
  void AddInput(FileInputNode* node);
};
}  // namespace hermes::audio
