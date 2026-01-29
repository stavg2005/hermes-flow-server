#pragma once
#include "Config.hpp"
#include "FileInputNode.hpp"

namespace hermes::audio {

/**
 * @brief Mixes multiple FileInputNode sources into a single audio stream.
 */
struct MixerNode : Node, IAudioProcessor {

  // Helper struct to cache the Interface pointer.
  // to avoids dynamic_cast<IAudioProcessor*> every 20ms.
  struct MixerSource {
    Node* node;              // For metadata (get_total_frames)
    IAudioProcessor* audio;  // For hot-path processing (process_frame)
  };
  std::vector<MixerSource> inputs_;
  std::array<int32_t, config::SAMPLES_PER_FRAME>
      accumulator_{}; /**< Intermediate mix buffer */
  std::array<uint8_t, config::FRAME_SIZE_BYTES>
      temp_input_buffer_{}; /**< Temp buffer for input frames */

  explicit MixerNode(Node* t = nullptr);


  IAudioProcessor* as_audio() override;
  std::expected<void, config::NodeError> process_frame(
      std::span<uint8_t> frame_buffer) override;
  std::expected<void, config::NodeError> close() override;

  virtual std::expected<void, config::NodeError> connect_input(
      Node* source) override;

  void set_max_frames();
  void add_input(FileInputNode* node);
};
}  // namespace hermes::audio
