#pragma once
#include <boost/json.hpp>
#include <string>
#include <variant>
#include <vector>

namespace bj = boost::json;

// -------- enums & helpers --------
enum class NodeKind { FileInput, Mixer, Delay, Clients, FileOptions };

inline NodeKind kind_from(std::string_view s) {
  if (s == "fileInput")
    return NodeKind::FileInput;
  if (s == "mixer")
    return NodeKind::Mixer;
  if (s == "delay")
    return NodeKind::Delay;
  if (s == "clients")
    return NodeKind::Clients;
  if (s == "fileOptions")
    return NodeKind::FileOptions;
  throw std::runtime_error("Unknown node type: " + std::string(s));
}

template <class T> inline T require(const bj::object &o, std::string_view key) {
  auto it = o.find(key);
  if (it == o.end())
    throw std::runtime_error("Missing key: " + std::string(key));
  return bj::value_to<T>(it->value());
}
template <class T>
inline T get_or(const bj::object &o, std::string_view key, T def) {
  if (auto it = o.find(key); it != o.end())
    return bj::value_to<T>(it->value());
  return def;
}

// -------- payloads we actually care about --------
struct Options {
  double gain{1.0};
};

struct FileItem {
  std::string filePath;
  std::string fileName;
  Options options{};
};

struct FileInputData {
  std::string fileName, filePath;
  Options options{};
};
struct MixerData {
  std::vector<FileItem> files;
};
struct DelayData {
  int delay{0};
};

struct Client {
  std::string id, name, ip,
      port; // ignore isSelected; engine can filter later if needed
};
struct ClientsData {
  std::vector<Client> clients;
};

struct FileOptionsData {
  double gain{1.0};
};

using NodeData = std::variant<FileInputData, MixerData, DelayData, ClientsData,
                              FileOptionsData>;

// -------- graph primitives (minimal) --------
struct Node {
  std::string id;
  NodeKind kind;
  NodeData data;
};

struct Edge {
  std::string id; // keep for debugging; optional to use
  std::string source;
  std::string sourceHandle;
  std::string target;
  std::string targetHandle;
};

struct Graph {
  std::vector<Node> nodes;
  std::vector<Edge> edges;
};
