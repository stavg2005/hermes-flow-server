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
  IAudioProcessor* AsAudio() override;
  std::expected<void, config::NodeError> ProcessFrame(
      std::span<uint8_t> frame_buffer) override;
  std::expected<void, config::NodeError> Close() override;
  virtual std::expected<void, config::NodeError> ConnectInput(
      std::shared_ptr<Node> source) override;
};
}  // namespace hermes::audio
