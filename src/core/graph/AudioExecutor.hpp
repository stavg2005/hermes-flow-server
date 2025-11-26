#pragma once
#include <boost/asio.hpp>
#include <memory>
#include <span>
#include <vector>

#include "S3Session.hpp"  // If S3 logic stays here
#include "config.hpp"
#include "Nodes.hpp"
#include "spdlog/spdlog.h"

class AudioExecutor {
   public:
    AudioExecutor(boost::asio::io_context& io, std::unique_ptr<Graph> graph)
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

    net::awaitable<void> FetchFiles() {
        spdlog::info("Fetching files");

        spdlog::info("Preparing Audio Graph...");
        // Move FetchFiles logic here
        for (const auto& node : graph_->nodes) {
            if (node->kind == NodeKind::FileInput) {
                auto* fileNode = dynamic_cast<FileInputNode*>(node.get());
                if (!fileNode) continue;

                // Reuse your existing S3 logic or the new 'FetchFiles' refactor
                if (!std::filesystem::exists(fileNode->file_path)) {
                    auto session = std::make_shared<S3Session>(io_);
                    co_await session->RequestFile(fileNode->file_name);
                }
                co_await fileNode->InitilizeBuffers();
            }
        }
    }

    // 1. Initialization Phase: Download files, setup buffers
    boost::asio::awaitable<void> Prepare() {
        spdlog::info("Preparing Audio Graph...");
        co_await FetchFiles();
        current_node_ = graph_->start_node;
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
                current_node_ = current_node_->target;
                spdlog::info("Transitioning to node: {}",
                             current_node_ ? current_node_->id : "END");
            }

            return (current_node_ != nullptr);
        }
    };

   private:
    boost::asio::io_context& io_;
    std::unique_ptr<Graph> graph_;
    Node* current_node_ = nullptr;
};
