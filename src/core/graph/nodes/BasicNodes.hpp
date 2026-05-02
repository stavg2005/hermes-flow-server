#pragma once

#include <algorithm>
#include <expected>
#include <string>
#include <unordered_map>

#include "Config.hpp"
#include "Node.hpp"

namespace hermes::audio {

// =========================================================
// FileOptionsNode: Configuration holder (Gain, etc.)
// =========================================================
struct FileOptionsNode : Node {
  double gain{1.0};
  double pitch_shift{1.0};

  // Logic defined inside class body is implicitly 'inline'
  explicit FileOptionsNode(Node* t = nullptr) : Node(t) {
    kind_ = NodeKind::FileOptions;
  }

  void set_in_loop(bool val) override { is_in_loop_ = val; };
};

// =========================================================
// ClientsNode: Registry of streaming targets
// =========================================================
struct ClientsNode : Node {
  std::unordered_map<std::string, uint16_t> clients;

  explicit ClientsNode(Node* t = nullptr) : Node(t) {
    kind_ = NodeKind::Clients;
  }

  void add_client(std::string ip, uint16_t port) {
    clients.emplace(std::move(ip), port);
  }

  void set_in_loop(bool val) override { is_in_loop_ = val; };
  // Override to enforce rule: Clients cannot have inputs
  std::expected<void, config::NodeError> connect_input(Node* source) override {
    return {};
    /*
        return error(config::NodeErrorCode::FormatError,
                 "ClientsNode cannot accept incoming connections.");
    */
  }
};

// =========================================================
// DelayNode: Inserts silence based on configuration
// =========================================================
struct DelayNode : Node, IAudioProcessor {
  float delay_ms_{0.0F};

  explicit DelayNode(Node* t = nullptr) : Node(t) { kind_ = NodeKind::Delay; }

  explicit DelayNode(size_t ms_delay) {
    kind_ = NodeKind::Delay;
    delay_ms_ = static_cast<float>(ms_delay);
    // Calculation moves here. Ensure Config.hpp is included!
    total_frames_ = static_cast<int>(delay_ms_ / config::FRAME_DURATION);
  }

  IAudioProcessor* as_audio() override { return this; }
  void set_in_loop(bool val) override { is_in_loop_ = val; };
  std::expected<void, config::NodeError> process_frame(
      std::span<uint8_t> frame_buffer) override {
    // Fill buffer with silence (0)
    std::fill(frame_buffer.begin(), frame_buffer.end(), 0);

    in_buffer_processed_frames_++;
    processed_frames_++;
    return {};
  }

  std::expected<void, config::NodeError> close() override {
    in_buffer_processed_frames_ = 0;
    processed_frames_ = 0;
    return {};
  }

  std::expected<void, config::NodeError> connect_input(Node* source) override {
    if (source->kind() == NodeKind::Clients) {
      return error(config::NodeErrorCode::FormatError,
                   "DelayNode cannot accept ClientsNode.");
    }
    // wire_standard is defined in Node.cpp, so we can just call it here
    wire_standard(source);
    return {};
  }
};

}  // namespace hermes::audio
