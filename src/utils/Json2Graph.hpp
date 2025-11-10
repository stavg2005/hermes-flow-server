#pragma once
#include "models/Nodes.hpp"

inline Options parse_options(const bj::object &o) {
  return Options{get_or<double>(o, "gain", 1.0)};
}
inline FileItem parse_file_item(const bj::object &o) {
  return FileItem{.filePath = require<std::string>(o, "filePath"),
                  .fileName = require<std::string>(o, "fileName"),
                  .options = o.if_contains("options")
                                 ? parse_options(o.at("options").as_object())
                                 : Options{}};
}
inline FileInputData parse_file_input(const bj::object &d) {
  return FileInputData{.fileName = require<std::string>(d, "fileName"),
                       .filePath = require<std::string>(d, "filePath"),
                       .options =
                           d.if_contains("options")
                               ? parse_options(d.at("options").as_object())
                               : Options{}};
}
inline MixerData parse_mixer(const bj::object &d) {
  MixerData m{};
  if (auto it = d.find("files"); it != d.end())
    for (auto &v : it->value().as_array())
      m.files.push_back(parse_file_item(v.as_object()));
  return m;
}
inline DelayData parse_delay(const bj::object &d) {
  return DelayData{require<int>(d, "delay")};
}
inline ClientsData parse_clients(const bj::object &d) {
  ClientsData out{};
  if (auto it = d.find("clients"); it != d.end()) {
    for (auto &v : it->value().as_array()) {
      const auto &o = v.as_object();
      out.clients.push_back(Client{.id = get_or<std::string>(o, "id", ""),
                                   .name = get_or<std::string>(o, "name", ""),
                                   .ip = get_or<std::string>(o, "ip", ""),
                                   .port = get_or<std::string>(o, "port", "")});
    }
  }
  return out;
}
inline FileOptionsData parse_file_options(const bj::object &d) {
  return FileOptionsData{get_or<double>(d, "gain", 1.0)};
}
inline NodeData parse_node_data(NodeKind k, const bj::object &d) {
  switch (k) {
  case NodeKind::FileInput:
    return parse_file_input(d);
  case NodeKind::Mixer:
    return parse_mixer(d);
  case NodeKind::Delay:
    return parse_delay(d);
  case NodeKind::Clients:
    return parse_clients(d);
  case NodeKind::FileOptions:
    return parse_file_options(d);
  }
  throw std::runtime_error("Unhandled NodeKind");
}
inline Node parse_node(const bj::object &o) {
  Node n;
  n.id = require<std::string>(o, "id");
  n.kind = kind_from(require<std::string>(o, "type"));
  n.data = parse_node_data(n.kind, require<bj::object>(o, "data"));
  return n;
}
inline Edge parse_edge(const bj::object &o) {
  return Edge{
      .id = get_or<std::string>(o, "id", ""),
      .source = require<std::string>(o, "source"),
      .sourceHandle = require<std::string>(o, "sourceHandle"),
      .target = require<std::string>(o, "target"),
      .targetHandle = require<std::string>(o, "targetHandle"),
  };
}
inline Graph parse_graph(const bj::object &o) {
  Graph g{};
  for (auto &v : require<bj::array>(o, "nodes"))
    g.nodes.push_back(parse_node(v.as_object()));
  for (auto &v : require<bj::array>(o, "edges"))
    g.edges.push_back(parse_edge(v.as_object()));
  return g;
}
