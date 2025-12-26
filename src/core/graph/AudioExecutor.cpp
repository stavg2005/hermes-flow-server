#include "AudioExecutor.hpp"

#include <algorithm>
#include <filesystem>
#include <stdexcept>

#include "S3Session.hpp"
#include "spdlog/spdlog.h"


AudioExecutor::AudioExecutor(boost::asio::io_context& io, std::shared_ptr<Graph> graph)
    : io_(io), graph_(std::move(graph)) {
    if (!graph_ || !graph_->start_node) {
        throw std::runtime_error("Invalid graph: missing start node.");
    }
    current_node_ = graph_->start_node;
}

SessionStats& AudioExecutor::get_stats() {
    return stats_;
}

boost::asio::awaitable<void> AudioExecutor::Prepare() {
    spdlog::info("Preparing Audio Graph...");

    // 1. Download missing files and init buffers
    co_await FetchFiles();

    // 2. Configure mixers
    UpdateMixers();

    // 3. Reset state
    current_node_ = graph_->start_node;
    stats_.current_node_id = current_node_->id;
    stats_.total_bytes_sent = 0;
}

boost::asio::awaitable<void> AudioExecutor::FetchFiles() {
    spdlog::info("Checking file requirements...");

    for (const auto& node : graph_->nodes) {
        if (node->kind == NodeKind::FileInput) {
            auto* file_node = static_cast<FileInputNode*>(node.get());

            // Check if file exists locally, otherwise download from S3
            if (!std::filesystem::exists(file_node->file_path)) {
                spdlog::info("File missing: {}. Requesting from S3...", file_node->file_name);
                auto s3_session = std::make_shared<S3Session>(io_);
                co_await s3_session->RequestFile(file_node->file_name);
            }

            // Initialize async buffers (open file, fill initial buffer)
            co_await file_node->InitializeBuffers();
        }
    }
}

void AudioExecutor::UpdateMixers() {
    for (const auto& node : graph_->nodes) {
        if (node->kind == NodeKind::Mixer) {
            if (auto* mixer = dynamic_cast<MixerNode*>(node.get())) {
                mixer->SetMaxFrames();
            }
        }
    }
}

bool AudioExecutor::GetNextFrame(std::span<uint8_t> output_buffer) {
    if (!current_node_) return false;

    // Zero out buffer (critical for mixing)
    std::fill(output_buffer.begin(), output_buffer.end(), 0);

    // Process current node
    if (auto* audio_node = current_node_->AsAudio()) {
        audio_node->ProcessFrame(output_buffer);

        // Check if current node is finished
        if (current_node_->processed_frames >= current_node_->total_frames) {
            // Close the current processor to release resources
            audio_node->Close();

            spdlog::info("Node [{}] finished. Transitions to [{}]", current_node_->id,
                         current_node_->target ? current_node_->target->id : "END");

            // Move to next node
            current_node_ = current_node_->target;

            if (current_node_) {
                stats_.current_node_id = current_node_->id;
            }
        }

        stats_.total_bytes_sent += FRAME_SIZE_BYTES;
        return (current_node_ != nullptr);
    }

    // Non-audio node encountered (shouldn't happen in simple chain)
    return false;
}
