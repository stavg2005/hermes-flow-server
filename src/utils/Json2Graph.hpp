#pragma once

#include <boost/json.hpp>

#include "Nodes.hpp"  // Assumed to contain Graph and Node definitions


namespace bj = boost::json;

/**
 * @brief Parses a boost::json object into a Graph structure.
 * @param o The root JSON object containing the "flow".
 * @return A complete Graph object.
 * @throws std::runtime_error on parsing errors or missing keys.
 */
Graph parse_graph(boost::asio::io_context& io,const bj::object& o);

/**
 * @brief Prints a human-readable representation of a Graph to std::cout.
 * @param graph The graph to print.
 */
void print_graph(const Graph& graph);
