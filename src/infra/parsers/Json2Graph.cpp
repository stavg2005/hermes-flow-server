#include "Json2Graph.hpp"

#include <memory>
#include <stdexcept>
#include <string>

#include "NodeFactory.hpp"

namespace {

// --- Helpers ---

template <class T>
T require(const json::object& obj, const char* key) {
    if (!obj.contains(key)) {
        throw std::runtime_error(std::string("Missing required key: ") + key);
    }
    try {
        return json::value_to<T>(obj.at(key));
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to parse key '") + key + "': " + e.what());
    }
}

}  // namespace

//  Graph Parser
Graph parse_graph(boost::asio::io_context& io, const json::object& o) {
    Graph g{};

    const json::object& flow_obj = require<json::object>(o, "flow");
    const json::array& nodes_arr = require<json::array>(flow_obj, "nodes");
    const json::array& edges_arr = require<json::array>(flow_obj, "edges");

    // --- Phase 1: Create Nodes ---
    for (const auto& v : nodes_arr) {
        const auto& node_obj = v.as_object();

        std::string id = require<std::string>(node_obj, "id");
        std::string type = require<std::string>(node_obj, "type");
        const auto& data = require<json::object>(node_obj, "data");

        // Factory creates the specific Node subclass
        auto new_node = NodeFactory::Instance().Create(type, io, data);
        new_node->id = id;

        g.node_map[id] = new_node;
        g.nodes.push_back(std::move(new_node));
    }

    // --- Phase 2: Set Start Node ---
    const json::object& start_obj = require<json::object>(flow_obj, "start_node");
    std::string start_id = require<std::string>(start_obj, "id");

    if (!g.node_map.contains(start_id)) {
        throw std::runtime_error("Start node ID not found: " + start_id);
    }
    g.start_node = g.node_map.at(start_id).get();

    // --- Phase 3: Link Edges ---
    for (const auto& v : edges_arr) {
        const auto& edge_obj = v.as_object();

        std::string source_id = require<std::string>(edge_obj, "source");
        std::string target_id = require<std::string>(edge_obj, "target");

        if (!g.node_map.contains(source_id)) {
            throw std::runtime_error("Missing source: " + source_id);
        }
        if (!g.node_map.contains(target_id)) {
            throw std::runtime_error("Missing target: " + target_id);
        }

        auto source = g.node_map.at(source_id);
        auto target = g.node_map.at(target_id);

        /* --------------------------------------------------------------------------
         * Special Edge Handling: Configuration vs. Data Flow
         * --------------------------------------------------------------------------
         * Standard edges represent the flow of Audio Data (PCM).
         * However, 'FileOptions' nodes represent Configuration Data (Gain, Trim, etc.).
         * * Instead of linking them as an audio source, we "inject" the options node
         * directly into the target FileInputNode. This allows the FileInputNode to
         * apply effects (like Gain) internally during its ProcessFrame loop.
         */
        if (source->kind == NodeKind::FileOptions && target->kind == NodeKind::FileInput) {
            auto opt = std::dynamic_pointer_cast<FileOptionsNode>(source);
            auto inp = std::dynamic_pointer_cast<FileInputNode>(target);
            if (opt && inp) inp->SetOptions(opt);
        }
        // Standard Audio Flow Edges
        else {
            source->target = target.get();

            // If target is a Mixer, register the input
            if (target->kind == NodeKind::Mixer && source->kind == NodeKind::FileInput) {
                auto mixer = std::dynamic_pointer_cast<MixerNode>(target);
                auto input = std::dynamic_pointer_cast<FileInputNode>(source);
                if (mixer && input) mixer->AddInput(input.get());
            }
        }
    }

    return g;
}
