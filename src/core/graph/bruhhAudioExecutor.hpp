#pragma once
/*
#include <boost/asio.hpp>
#include <memory>
#include <span>
#include <variant>
#include <vector>

#include "Nodes.hpp"  // Make sure this is your new header with the variant
#include "S3Session.hpp"
#include "WebSocketSessionObserver.hpp"
#include "config.hpp"
#include "spdlog/spdlog.h"

// --- Helper for std::visit ---
// (You can move this to a utility header if used elsewhere)
template <class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

class AudioExecutor {
   public:
    AudioExecutor(boost::asio::io_context& io, std::shared_ptr<Graph> graph)
        : io_(io), graph_(std::move(graph)) {
        current_node_ = graph_->start_node;
    }

    Node* get_start_node() {
        if (!graph_->start_node) {
            throw std::runtime_error("Graph has no valid start_node set.");
        }
        return graph_->start_node;
    }

    SessionStats& get_stats() { return stats; }

    // 1. Initialization Phase: Download files, setup buffers
    boost::asio::awaitable<void> Prepare() {
        spdlog::info("Preparing Audio Graph...");
        co_await FetchFiles();
        UpdateMixers();

        current_node_ = graph_->start_node;
        if (current_node_) {
            stats.current_node_id = current_node_->id;
        }
        stats.total_bytes_sent = 0;
    }

    // Logic to download files if they are missing
    boost::asio::awaitable<void> FetchFiles() {
        spdlog::info("Fetching files");

        for (const auto& node_ptr : graph_->nodes) {  // node_ptr is shared_ptr<Node>
            if (auto* fileInput = std::get_if<FileInput>(&node_ptr->data)) {
                // 1. Download if missing
                if (!std::filesystem::exists(fileInput->file_path)) {
                    auto session = std::make_shared<S3Session>(io_);
                    co_await session->RequestFile(fileInput->file_name);
                    fileInput->file_path = "downloads/" + fileInput->file_name;
                }

                // 2. Initialize and UPDATE the wrapper's total_frames
                // We pass node_ptr->total_frames by reference!
                co_await fileInput->InitilizeBuffers(node_ptr->total_frames);
            }
        }
    }

    // Logic to calculate total frames for mixers
    void UpdateMixers() {
        for (const auto& node_ptr : graph_->nodes) {
            // Check if this node holds Mixer data
            if (auto* mixer = std::get_if<Mixer>(&node_ptr->data)) {
                // Pass the wrapper's total_frames by reference so Mixer can update it
                mixer->SetMaxFrames(node_ptr->total_frames);
            }
        }
    }

    // 2. Execution Phase: Fill a buffer with the next frame
    bool GetNextFrame(std::span<uint8_t> output_buffer) {
        if (!current_node_) return false;

        // Clear buffer before processing
        std::fill(output_buffer.begin(), output_buffer.end(), 0);

        // --- POLYMORPHIC DISPATCH (The Magic) ---
        std::visit(overloaded{[&](FileInput& n) { n.ProcessFrame(output_buffer, *current_node_); },
                              [&](Mixer& n) {
                                  // Ensure Mixer::ProcessFrame accepts (buffer, parent) signature
                                  // too!
                                  n.ProcessFrame(output_buffer, *current_node_);
                              },
                              [&](Delay& n) {
                                  // Ensure Delay::ProcessFrame accepts (buffer, parent) signature
                                  // too!
                                  n.ProcessFrame(output_buffer, *current_node_);
                              },
                              [&](auto& other) {
                                  // ClientsNode, FileOptions, etc. do not produce audio.
                                  // We treat them as "instant finish" or no-op.
                              }},
                   current_node_->data);

        // --- TRANSITION LOGIC ---
        // The struct (FileInput/Mixer) is responsible for incrementing
        // current_node_->processed_frames via the parent reference.
        if (current_node_->processed_frames >= current_node_->total_frames) {

            std::visit([](auto& n){ if constexpr(requires{n.Close();}) n.Close(); },
            current_node_->data);

            // Move to next node
            current_node_ = current_node_->target;

            if (current_node_) {
                stats.current_node_id = current_node_->id;
                spdlog::info("Transitioning to node: {}", current_node_->id);
            } else {
                spdlog::info("Graph execution finished.");
            }
        }

        stats.total_bytes_sent += FRAME_SIZE_BYTES;
        return (current_node_ != nullptr);
    };

   private:
    boost::asio::io_context& io_;
    std::shared_ptr<Graph> graph_;
    Node* current_node_ = nullptr;
    SessionStats stats;
};
*/
