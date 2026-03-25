#include "Json2Graph.hpp"

#include <expected>
#include <memory>
#include <string>

#include "NodeFactory.hpp"
#include "Types.hpp"
#include "JsonUtils.hpp"
#include "spdlog/spdlog.h"
using namespace hermes::audio;
using namespace hermes::infra;
using namespace hermes::config;
namespace hermes::infra {

std::expected<void, config::ErrorInfo> parse_nodes(boost::asio::io_context& io,
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

    if (!id) return std::unexpected(id.error());
    if (!type) return std::unexpected(type.error());
    if (!data) return std::unexpected(data.error());

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

std::expected<void, config::ErrorInfo> set_start_node(
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

std::expected<void, config::ErrorInfo> parse_edges(const json::array& edges_arr,
                                                   audio::Graph& graph) {
  for (const auto& v : edges_arr) {
    if (!v.is_object()) {
      return std::unexpected(
          ErrorInfo::From(AppError::ParseError, "Edge must be an object"));
    }
    const auto& edge = v.as_object();

    auto src_res = require_json<std::string>(edge, "source");
    auto tgt_res = require_json<std::string>(edge, "target");
    if (!src_res) return std::unexpected(src_res.error());
    if (!tgt_res) return std::unexpected(tgt_res.error());

    const std::string& src_id = *src_res;
    const std::string& tgt_id = *tgt_res;

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
                  return parse_nodes(io, nodes, graph);
                })

                .and_then([&] { return set_start_node(flow, graph); })

                .and_then(
                    [&] { return require_json<json::array>(flow, "edges"); })
                .and_then([&](const json::array& edges) {
                  return parse_edges(edges, graph);
                });
          })

      .transform([&] { return std::move(graph); });
}
}  // namespace hermes::infra
