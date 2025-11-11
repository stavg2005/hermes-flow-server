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
  boost::asio::random_access_file file_;
  const std::string &path;
  int bytes_read{0};
  // 500kb buffers
  std::array<uint8_t, (1024 * 512)> block_1{0};

  std::array<uint8_t, (1024 * 512)> block_2{0};
};
struct Node {
  Node(Node *t) : target(t) {}
  std::string id;
  NodeKind kind;
  int processed_frames{0};
  int total_frames{0};
  Node *target;

  std::unique_ptr<
      std::unordered_map<std::string, std::unique_ptr<Double_Buffer>>>
      files_buffers;
  virtual void ProcessFrame(std::span<uint8_t, 160> &frame_buffer) = 0;
  virtual ~Node() = default;
};

struct MixerNode : Node {
  void ProcessFrame(std::span<uint8_t, 160> &frame_buffer) override {
    
  }
};

struct Edge {
  std::string id;  // keep for debugging; optional to use
  std::string source;
  std::string sourceHandle;
  std::string target;
  std::string targetHandle;
};

struct Graph {
  std::vector<Node> nodes;
  std::vector<Edge> edges;
};
