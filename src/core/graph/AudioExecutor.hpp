#pragma once
#include <boost/asio.hpp>
#include <memory>
#include <span>
#include <vector>

#include "Nodes.hpp"
#include "S3Session.hpp"  // If S3 logic stays here
#include "WebSocketSessionObserver.hpp"
#include "config.hpp"
#include "spdlog/spdlog.h"

class AudioExecutor {
   public:
    AudioExecutor(boost::asio::io_context& io, std::shared_ptr<Graph> graph)
        : io_(io), graph_(std::move(graph)) {
        current_node_ = graph_->start_node;
    }

    Node* get_start_node() {
        if (graph_->start_node == nullptr) {
            throw std::runtime_error("Graph has no valid start_node set.");
        }
        return graph_->start_node;
    }

    bool FileExist(std::string& file_name) {
        // TODO actually check if file already in directory
        return false;
    }

    SessionStats& get_stats() { return stats; }
    net::awaitable<void> FetchFiles() {
        spdlog::info("Fetching files");

        spdlog::info("Preparing Audio Graph...");
        // Move FetchFiles logic here
        for (const auto& node : graph_->nodes) {
            if (node->kind == NodeKind::FileInput) {
                auto* fileNode = dynamic_cast<FileInputNode*>(node.get());
                if (!fileNode) continue;

                if (!std::filesystem::exists(fileNode->file_path)) {
                    auto session = std::make_shared<S3Session>(io_);
                    co_await session->RequestFile(fileNode->file_name);
                }
                co_await fileNode->InitilizeBuffers();
            }
        }
    }

    void UpdateMixers() {
        for (const auto& node : graph_->nodes) {
            if (node->kind == NodeKind::Mixer) {
                auto* mixer = dynamic_cast<MixerNode*>(node.get());
                if (!mixer) {
                    continue;
                }
                mixer->SetMaxFrames();
            }
        }
    }
    // 1. Initialization Phase: Download files, setup buffers
    boost::asio::awaitable<void> Prepare() {
        spdlog::info("Preparing Audio Graph...");
        co_await FetchFiles();
        UpdateMixers();
        current_node_ = graph_->start_node;
        stats.current_node_id = current_node_->id;
        stats.total_bytes_sent = 0;
    }

    // 2. Execution Phase: Fill a buffer with the next frame
    // Returns false if the graph is finished
    bool GetNextFrame(std::span<uint8_t> output_buffer) {
        if (!current_node_) return false;

        // Clear buffer before processing (important for mixing!)
        std::fill(output_buffer.begin(), output_buffer.end(), 0);

        if (auto* audioNode = current_node_->AsAudio()) {
            audioNode->ProcessFrame(output_buffer);

            // Check if node is done
            if (current_node_->processed_frames >= current_node_->total_frames) {
                if (auto* bruh = current_node_->AsAudio()) {
                    bruh->Close();
                }
                current_node_ = current_node_->target;
                stats.current_node_id = current_node_->id;
                spdlog::info("Transitioning to node: {}",
                             current_node_ ? current_node_->id : "END");
            }
            stats.total_bytes_sent += FRAME_SIZE_BYTES;
            return (current_node_ != nullptr);
        }
    };

   private:
    boost::asio::io_context& io_;
    std::shared_ptr<Graph> graph_;
    Node* current_node_ = nullptr;
    SessionStats stats;
};
