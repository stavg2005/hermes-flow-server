#include "Json2Graph.hpp"

#include <expected>
#include <memory>
#include <string>

#include "NodeFactory.hpp"
#include "Types.hpp"  // Ensure ErrorInfo/AppError are available
#include "spdlog/spdlog.h"
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

std::expected<void, config::ErrorInfo> ParseNodes(boost::asio::io_context& io,
                                                  const json::array& nodes_arr,
                                                  audio::Graph& graph) {
  for (const auto& v : nodes_arr) {
    if (!v.is_object())
      return std::unexpected(
          ErrorInfo::From(AppError::ParseError, "Node must be an object"));

    const auto& obj = v.as_object();

    auto id = require_json<std::string>(obj, "id");
    auto type = require_json<std::string>(obj, "type");
    auto data = require_json<json::object>(obj, "data");

    if (!id || !type || !data) {
      return std::unexpected(id ? (type ? data.error() : type.error())
                                : id.error());
    }

    auto node_res = audio::NodeFactory::instance().create(*type, io, *data);
    if (!node_res) {
      return std::unexpected(node_res.error());
    }

    auto node = std::move(*node_res);

    node->set_id(*id);
    graph.node_map[*id] = node.get();
    graph.nodes.push_back(std::move(node));
  }
  return {};
}

std::expected<void, config::ErrorInfo> SetStartNode(
    const json::object& flow_obj, audio::Graph& graph) {
  auto start_obj_res = require_json<json::object>(flow_obj, "start_node");
  if (!start_obj_res) {
    return std::unexpected(start_obj_res.error());
  }

  auto id_res = require_json<std::string>(*start_obj_res, "id");
  if (!id_res) {
    return std::unexpected(id_res.error());
  }

  if (!graph.node_map.contains(*id_res)) {
    return std::unexpected(ErrorInfo::From(
        AppError::ParseError, "Start node ID not found: " + *id_res));
  }

  graph.start_node = graph.node_map.at(*id_res);
  spdlog::debug("startn node in graph: {}", graph.start_node->id());
  return {};
}

std::expected<void, config::ErrorInfo> ParseEdges(const json::array& edges_arr,
                                                  audio::Graph& graph) {


  for (const auto& v : edges_arr) {
    const auto& edge = v.as_object();

    std::string src_id = std::string(edge.at("source").as_string());
    std::string tgt_id = std::string(edge.at("target").as_string());

    if (!graph.node_map.contains(src_id) || !graph.node_map.contains(tgt_id)) {
      return std::unexpected(ErrorInfo::From(
          AppError::ParseError, "Edge references missing node ID"));
    }

    auto* src_node = graph.node_map[src_id];
    auto* tgt_node = graph.node_map[tgt_id];

    if (src_node->kind() == audio::NodeKind::Clients) {
      return std::unexpected(ErrorInfo::From(
          AppError::ParseError, "ClientsNode cannot be a source."));
    }

    auto result = tgt_node->connect_input(src_node);
    if (!result) {
      return std::unexpected(
          ErrorInfo::From(AppError::ParseError, result.error().message));
    }
  }
  return {};
}



std::expected<audio::Graph, config::ErrorInfo> parse_graph(
    boost::asio::io_context& io, const json::object& o) {
  audio::Graph graph{};

  return require_json<json::object>(o, "flow")
      .and_then(
          [&](const json::object& flow) -> std::expected<void, ErrorInfo> {
            return require_json<json::array>(flow, "nodes")
                .and_then([&](const json::array& nodes) {
                  return ParseNodes(io, nodes, graph);
                })

                .and_then([&] { return SetStartNode(flow, graph); })

                .and_then(
                    [&] { return require_json<json::array>(flow, "edges"); })
                .and_then([&](const json::array& edges) {
                  return ParseEdges(edges, graph);
                });
          })

      .transform([&] { return std::move(graph); });
}
}  // namespace hermes::infra
