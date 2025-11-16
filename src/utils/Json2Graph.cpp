#include "Json2Graph.hpp"

// All other necessary headers go here
#include <iostream>  // For std::cout
#include <stdexcept>
#include <string>
#include <string_view>  // For std::string_view

namespace bj = boost::json;

//=============================================================================
// ANONYMOUS NAMESPACE (PRIVATE HELPERS)
//
// All functions in here are private to this file.
// This prevents linker errors.
//=============================================================================
namespace {

// --- JSON Helper Functions ---

template <class T>
T require(const bj::object& obj, const char* key) {
  if (!obj.contains(key)) {
    throw std::runtime_error(std::string("Missing required key: ") + key);
  }
  try {
    return bj::value_to<T>(obj.at(key));
  } catch (const std::exception& e) {
    throw std::runtime_error(std::string("Failed to parse key '") + key +
                             "': " + e.what());
  }
}

template <class T>
T get_or(const bj::object& obj, const char* key, T default_val) {
  if (!obj.contains(key) || obj.at(key).is_null()) {
    return default_val;
  }
  try {
    return bj::value_to<T>(obj.at(key));
  } catch (...) {
    return default_val;  // Return default on parsing error
  }
}

double parse_gain_from_options(const bj::value* options_val) {
  if (!options_val || !options_val->is_object()) {
    return 1.0;  // Default gain if options are null or not an object
  }
  return get_or<double>(options_val->as_object(), "gain", 1.0);
}

// --- Node Factory ---

std::unique_ptr<Node> create_node(NodeKind kind, const bj::object& data) {
  switch (kind) {
    case NodeKind::FileInput: {
      auto node = std::make_unique<FileInputNode>();
      node->file_name = require<std::string>(data, "fileName");
      node->file_path = require<std::string>(data, "filePath");
      node->bf.path = node->file_path;

      return node;
    }

    case NodeKind::Mixer: {
      auto node = std::make_unique<MixerNode>();
      if (const auto* it = data.find("files"); it != data.end()) {
        for (const auto& v : it->value().as_array()) {
          const auto& file_item = v.as_object();
          auto buffer = std::make_unique<Double_Buffer>();
          buffer->path = require<std::string>(file_item, "filePath");
          buffer->gain =
              parse_gain_from_options(file_item.if_contains("options"));
          node->files_buffers[buffer->path] = std::move(buffer);
        }
      }
      return node;
    }

    case NodeKind::Delay: {
      auto node = std::make_unique<DelayNode>();
      node->delay_ms = require<int>(data, "delay");
      return node;
    }

    case NodeKind::Clients: {
      auto node = std::make_unique<ClientsNode>();
      // TODO: Parse client-specific data
      return node;
    }

    case NodeKind::FileOptions: {
      auto node = std::make_unique<FileOptionsNode>();
      // This node's data *is* the gain
      node->gain = get_or<double>(data, "gain", 1.0);
      return node;
    }
  }
  // Added a fallback for safety, though kind_from should prevent this.
  throw std::runtime_error("Unhandled NodeKind in create_node");
}

// --- String Helper ---

std::string_view to_string(NodeKind kind) {
  switch (kind) {
    case NodeKind::FileInput:
      return "FileInput";
    case NodeKind::Mixer:
      return "Mixer";
    case NodeKind::Delay:
      return "Delay";
    case NodeKind::Clients:
      return "Clients";
    case NodeKind::FileOptions:
      return "FileOptions";
  }
  return "Unknown";
}

}  // End anonymous namespace

//=============================================================================
// PUBLIC FUNCTION DEFINITIONS
//=============================================================================

Graph parse_graph(const bj::object& o) {
  Graph g{};

  const bj::object& flow_obj = require<bj::object>(o, "flow");

  // --- 1. First Pass: Create all Nodes ---
  for (auto& v : require<bj::array>(flow_obj, "nodes")) {
    const auto& node_obj = v.as_object();

    std::string id = require<std::string>(node_obj, "id");
    NodeKind kind = kind_from(require<std::string>(node_obj, "type"));
    const auto& data = require<bj::object>(node_obj, "data");

    std::unique_ptr<Node> new_node = create_node(kind, data);
    new_node->id = id;
    new_node->kind = kind;

    g.node_map[id] = new_node.get();
    g.nodes.push_back(std::move(new_node));
  }
  // --- 2. Set the Start Node ---
  const bj::object& start_node_obj =
      require<bj::object>(flow_obj, "start_node");
  std::string start_node_id = require<std::string>(start_node_obj, "id");

  if (!g.node_map.contains(start_node_id)) {
    throw std::runtime_error("Start node ID not found in node map: " +
                             start_node_id);
  }
  g.start_node = g.node_map.at(start_node_id);

  // --- 3. Second Pass: Link Nodes via Edges ---
  for (auto& v : require<bj::array>(flow_obj, "edges")) {
    const auto& edge_obj = v.as_object();

    std::string source_id = require<std::string>(edge_obj, "source");
    std::string target_id = require<std::string>(edge_obj, "target");

    if (!g.node_map.contains(source_id)) {
      throw std::runtime_error("Edge source ID not found: " + source_id);
    }
    if (!g.node_map.contains(target_id)) {
      throw std::runtime_error("Edge target ID not found: " + target_id);
    }

    Node* source_node = g.node_map.at(source_id);
    Node* target_node = g.node_map.at(target_id);

    // --- UPDATED LOGIC ---
    // Check if this is a "settings" edge from FileOptions -> FileInput
    if (source_node->kind == NodeKind::FileOptions &&
        target_node->kind == NodeKind::FileInput) {
      // This is a settings edge. Apply the gain.
      auto* options_node = static_cast<FileOptionsNode*>(source_node);
      auto* file_node = static_cast<FileInputNode*>(target_node);
      //  TODO ADD MORE OPTIONS
      file_node->gain = options_node->gain;

      source_node->target = target_node;
    } else {
      source_node->target = target_node;
    }
  }

  return g;
}

void print_graph(const Graph& graph) {
  std::cout << "=========================\n";
  std::cout << "     PARSED GRAPH      \n";
  std::cout << "=========================\n";
  std::cout << "Total Nodes: " << graph.nodes.size() << "\n\n";

  for (const auto& node_ptr : graph.nodes) {
    const Node* node = node_ptr.get();
    if (!node) {
      std::cout << "--- [Null Node Pointer] ---\n";
      continue;
    }

    std::cout << "--- Node [" << node->id << "] ---\n";
    std::cout << "  Type:   " << to_string(node->kind) << "\n";

    if (node->target) {
      std::cout << "  Target: " << node->target->id << "\n";
    } else {
      std::cout << "  Target: [None]\n";
    }

    std::cout << "  Data:   "
              << "\n";
    switch (node->kind) {
      case NodeKind::FileInput: {
        const auto* file_node = static_cast<const FileInputNode*>(node);
        std::cout << "    - file_name: " << file_node->file_name << "\n";
        std::cout << "    - file_path: " << file_node->file_path << "\f";
        std::cout << "    - gain: " << file_node->gain << "\n";
        break;
      }
      case NodeKind::Mixer: {
        const auto* mixer_node = static_cast<const MixerNode*>(node);
        std::cout << "    - files_buffers (" << mixer_node->files_buffers.size()
                  << "):\n";
        for (const auto& [path, buffer_ptr] : mixer_node->files_buffers) {
          std::cout << "      - " << path << " (Gain: " << buffer_ptr->gain
                    << ")\n";
        }
        break;
      }
      case NodeKind::Delay: {
        const auto* delay_node = static_cast<const DelayNode*>(node);
        std::cout << "    - delay_ms: " << delay_node->delay_ms << "\n";
        break;
      }
      case NodeKind::Clients: {
        std::cout << "    - (Clients node data...)\n";
        break;
      }
      case NodeKind::FileOptions: {
        const auto* options_node = static_cast<const FileOptionsNode*>(node);
        std::cout << "    - gain: " << options_node->gain << "\n";
        break;
      }
    }
    std::cout << "\n";
  }
  std::cout << "=========================\n";
}
