#pragma once

#include <boost/json.hpp>

#include "Nodes.hpp"  // Assumed to contain Graph and Node definitions
#include "types.hpp"



/**
 * @brief Parses a boost::json object into a Graph structure.
 * @param io The IO context to assign to created nodes.
 * @param o  The root JSON object containing the "flow" definition.
 * @return   A complete Graph object with linked nodes.
 * @throws   std::runtime_error on parsing errors, missing keys, or invalid edges.
 */
Graph parse_graph(boost::asio::io_context& io,const json::object& o);

/**
 * @brief Prints a human-readable representation of a Graph to std::cout.
 * @param graph The graph to print.
 */
void print_graph(const Graph& graph);
