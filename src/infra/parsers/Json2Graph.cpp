#include "Json2Graph.hpp"

#include <memory>
#include <string>

#include "NodeFactory.hpp"
#include "Types.hpp"  // Ensure ErrorInfo/AppError are available
using namespace hermes::audio;
using namespace hermes::infra;
using namespace hermes::config;
namespace hermes::infra {

/**
 * @brief Helper that returns expected value or ErrorInfo.
 * Catches internal boost::json exceptions to maintain the expected interface.
 */
template <class T>
std::expected<T, ErrorInfo> require_json(const json::object& obj,
                                         const char* key) {
  if (!obj.contains(key)) {
    return std::unexpected(ErrorInfo::From(
        AppError::ParseError, "Missing required key: " + std::string(key)));
  }

  try {
    return json::value_to<T>(obj.at(key));
  } catch (const std::exception& e) {
    return std::unexpected(ErrorInfo::From(
        AppError::ParseError,
        "Invalid type for key '" + std::string(key) + "': " + e.what()));
  }
}

std::expected<Graph, ErrorInfo> parse_graph(boost::asio::io_context& io,
                                            const json::object& o) {
  Graph g{};

  // 1. Parse Root Objects
  auto flow_res = require_json<json::object>(o, "flow");
  if (!flow_res) return std::unexpected(flow_res.error());
  const json::object& flow_obj = *flow_res;

  auto nodes_res = require_json<json::array>(flow_obj, "nodes");
  if (!nodes_res) return std::unexpected(nodes_res.error());
  const json::array& nodes_arr = *nodes_res;

  auto edges_res = require_json<json::array>(flow_obj, "edges");
  if (!edges_res) return std::unexpected(edges_res.error());
  const json::array& edges_arr = *edges_res;

  // --- Phase 1: Create Nodes ---
  for (const auto& v : nodes_arr) {
    if (!v.is_object()) {
      return std::unexpected(
          ErrorInfo::From(AppError::ParseError, "Node must be a JSON object"));
    }
    const auto& node_obj = v.as_object();

    auto id_res = require_json<std::string>(node_obj, "id");
    if (!id_res) return std::unexpected(id_res.error());
    std::string id = *id_res;

    auto type_res = require_json<std::string>(node_obj, "type");
    if (!type_res) return std::unexpected(type_res.error());
    std::string type = *type_res;

    auto data_res = require_json<json::object>(node_obj, "data");
    if (!data_res) return std::unexpected(data_res.error());
    const auto& data = *data_res;

    // Create Node via Factory
    auto node_result = NodeFactory::Instance().Create(type, io, data);
    if (!node_result) {
      return std::unexpected(node_result.error());
    }

    auto new_node = std::move(*node_result);
    new_node->id_ = id;
    g.node_map[id] = new_node;
    g.nodes.push_back(std::move(new_node));
  }

  // --- Phase 2: Set Start Node ---
  auto start_obj_res = require_json<json::object>(flow_obj, "start_node");
  if (!start_obj_res) return std::unexpected(start_obj_res.error());

  auto start_id_res = require_json<std::string>(*start_obj_res, "id");
  if (!start_id_res) return std::unexpected(start_id_res.error());
  std::string start_id = *start_id_res;

  if (!g.node_map.contains(start_id)) {
    return std::unexpected(ErrorInfo::From(
        AppError::ParseError, "Start node ID not found: " + start_id));
  }
  g.start_node = g.node_map.at(start_id).get();

  // --- Phase 3: Link Edges ---
  for (const auto& v : edges_arr) {
    if (!v.is_object()) {
      return std::unexpected(
          ErrorInfo::From(AppError::ParseError, "Edge must be a JSON object"));
    }
    const auto& edge_obj = v.as_object();

    auto src_res = require_json<std::string>(edge_obj, "source");
    if (!src_res) return std::unexpected(src_res.error());
    std::string source_id = *src_res;

    auto tgt_res = require_json<std::string>(edge_obj, "target");
    if (!tgt_res) return std::unexpected(tgt_res.error());
    std::string target_id = *tgt_res;

    // Validate Existence
    if (!g.node_map.contains(source_id))
      return std::unexpected(ErrorInfo::From(AppError::ParseError,
                                             "Missing source: " + source_id));
    if (!g.node_map.contains(target_id))
      return std::unexpected(ErrorInfo::From(AppError::ParseError,
                                             "Missing target: " + target_id));

    auto source = g.node_map.at(source_id);
    auto target = g.node_map.at(target_id);

    // Special case: Options nodes are not audio sources.
    if (source->kind_ == NodeKind::FileOptions &&
        target->kind_ == NodeKind::FileInput) {
      auto opt = std::dynamic_pointer_cast<FileOptionsNode>(source);
      auto inp = std::dynamic_pointer_cast<FileInputNode>(target);
      if (opt && inp) inp->SetOptions(opt);
    } else {
      source->target_ = target.get();
      if (target->kind_ == NodeKind::Mixer &&
          source->kind_ == NodeKind::FileInput) {
        auto mixer = std::dynamic_pointer_cast<MixerNode>(target);
        auto input = std::dynamic_pointer_cast<FileInputNode>(source);
        if (mixer && input) mixer->AddInput(input.get());
      }
    }
  }

  return g;
}
}  // namespace hermes::infra
