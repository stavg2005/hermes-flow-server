#pragma once
#include <array>
#include <boost/asio/random_access_file.hpp>
#include <boost/json.hpp>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace bj = boost::json;

// -------- enums & helpers --------
enum class NodeKind { FileInput, Mixer, Delay, Clients, FileOptions };

inline NodeKind kind_from(std::string_view s) {
  if (s == "fileInput") return NodeKind::FileInput;
  if (s == "mixer") return NodeKind::Mixer;
  if (s == "delay") return NodeKind::Delay;
  if (s == "clients") return NodeKind::Clients;
  if (s == "fileOptions") return NodeKind::FileOptions;
  throw std::runtime_error("Unknown node type: " + std::string(s));
}

struct Double_Buffer {
  std::string path;
  int bytes_read{0};

  // --- NEW ---
  // Added gain, which will be parsed from the options
  double gain{1.0};

  // 500kb buffers
  std::array<uint8_t, (1024 * 512)> block_1{0};
  std::array<uint8_t, (1024 * 512)> block_2{0};
};

struct Node {
  Node(Node *t = nullptr) : target(t) {}
  std::string id;
  NodeKind kind;
  int processed_frames{0};
  int total_frames{0};
  Node *target;

  virtual void ProcessFrame(std::span<uint8_t, 160> &frame_buffer) = 0;
  virtual ~Node() = default;
};

struct MixerNode : Node {
  // Stores buffers for all files associated with this mixer
  std::unordered_map<std::string, std::unique_ptr<Double_Buffer>> files_buffers;

  MixerNode(Node *t = nullptr) : Node(t) {}
  void ProcessFrame(std::span<uint8_t, 160> &frame_buffer) override {
    // Mixer logic...
  }
};

struct FileInputNode : Node {
  std::string file_name;
  std::string file_path;
  Double_Buffer bf;

  // --- NEW ---
  // Added gain, which will be parsed from the options
  double gain{1.0};

  FileInputNode(Node *t = nullptr) : Node(t) {}
  void ProcessFrame(std::span<uint8_t, 160> &frame_buffer) override {
    // File input logic...
  }
};

struct DelayNode : Node {
  int delay_ms{0};
  DelayNode(Node *t = nullptr) : Node(t) {}
  void ProcessFrame(std::span<uint8_t, 160> &frame_buffer) override {
    // Delay logic...
  }
};

struct ClientsNode : Node {
  // Add client data members here...
  ClientsNode(Node *t = nullptr) : Node(t) {}
  void ProcessFrame(std::span<uint8_t, 160> &frame_buffer) override {
    // Client output logic...
  }
};

struct FileOptionsNode : Node {
  // This node *is* the gain, so it's parsed from data.gain
  double gain{1.0};
  FileOptionsNode(Node *t = nullptr) : Node(t) {}
  void ProcessFrame(std::span<uint8_t, 160> &frame_buffer) override {
    // Options logic...
  }
};

struct Graph {
  std::vector<std::unique_ptr<Node>> nodes;
  std::unordered_map<std::string, Node *> node_map;
};
