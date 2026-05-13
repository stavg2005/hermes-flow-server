#pragma once
#include "Config.hpp"
#include "FileInputNode.hpp"

namespace hermes::audio {

/**
 * @brief Mixes multiple FileInputNode sources into a single audio stream.
 */
struct MixerNode : Node {

  std::vector<Node*> inputs_;
  std::array<int32_t, config::SAMPLES_PER_FRAME>
      accumulator_{}; /**< Intermediate mix buffer */
  std::array<uint8_t, config::FRAME_SIZE_BYTES>
      temp_input_buffer_{}; /**< Temp buffer for input frames */

  explicit MixerNode(Node* t = nullptr);

  virtual void set_in_loop(bool val) override;
  std::expected<void, config::NodeError> process_frame(
      std::span<uint8_t> frame_buffer) override;
  std::expected<void, config::NodeError> close() override;

  virtual std::expected<void, config::NodeError> connect_input(
      Node* source) override;

  void set_max_frames();
  void add_input(FileInputNode* node);
};
}  // namespace hermes::audio
