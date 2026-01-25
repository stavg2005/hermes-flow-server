#pragma once
#include <stdlib.h>

#include "Node.hpp"

namespace hermes::audio {

/**
 * @brief Inserts silence or delay into the audio stream.
 */
struct DelayNode : Node, IAudioProcessor {
  float delay_ms_{0.0F};

  explicit DelayNode(Node* t = nullptr);

  DelayNode(size_t ms_delay);
  IAudioProcessor* as_audio() override;
  std::expected<void, config::NodeError> process_frame(
      std::span<uint8_t> frame_buffer) override;
  std::expected<void, config::NodeError> close() override;
  virtual std::expected<void, config::NodeError> connect_input(
      std::shared_ptr<Node> source) override;
};
}  // namespace hermes::audio
