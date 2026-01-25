#pragma once

#include <boost/json.hpp>

#include "Nodes.hpp"
#include "Types.hpp"

namespace hermes::infra {
/**
 * @brief Parses a boost::json object into a Graph structure.
 * @param io The IO context to assign to created nodes.
 * @param o  The root JSON object containing the "flow" definition.
 * @return   A complete Graph object with linked nodes.
 */
std::expected<hermes::audio::Graph, config::ErrorInfo> parse_graph(
    boost::asio::io_context& io, const boost::json::object& o);

std::expected<void, config::ErrorInfo> ParseNodes(
    boost::asio::io_context& io, const boost::json::array& nodes_arr,
    audio::Graph& graph);
std::expected<void, config::ErrorInfo> SetStartNode(
    const boost::json::object& flow_obj, audio::Graph& graph);

std::expected<void, config::ErrorInfo> ParseEdges(
    const boost::json::array& edges_arr, audio::Graph& graph);
/**
 * @brief Prints a human-readable representation of a Graph to std::cout.
 * @param graph The graph to print.
 */
void print_graph(const hermes::audio::Graph& graph);
}  // namespace hermes::infra
