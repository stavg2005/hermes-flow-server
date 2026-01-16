#include "AudioExecutor.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <stdexcept>

#include "Nodes.hpp"
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

    co_await FetchFiles();

    UpdateMixers();

    // Reset state
    current_node_ = graph_->start_node;
    stats_.current_node_id = current_node_->id_;
    stats_.total_bytes_sent = 0;
}

boost::asio::awaitable<void> AudioExecutor::FetchFiles() {
    spdlog::info("Checking file requirements...");

    for (const auto& node : graph_->nodes) {
        if (node->kind_ == NodeKind::FileInput) {
            auto* file_node = static_cast<FileInputNode*>(node.get());

            if (!std::filesystem::exists(file_node->file_path_)) {
                spdlog::info("File missing: {}. Requesting from S3...", file_node->file_name_);
                // Use shared_ptr to keep session alive during async operation.
                auto s3_session = std::make_shared<S3Session>(io_);
                co_await s3_session->request_file(file_node->file_name_);
            }

            co_await file_node->initialize_buffers();
        }
    }
}

void AudioExecutor::UpdateMixers() {
    for (const auto& node : graph_->nodes) {
        if (node->kind_ == NodeKind::Mixer) {
            if (auto* mixer = dynamic_cast<MixerNode*>(node.get())) {
                mixer->set_max_frames();
            }
        }
    }
}

std::pair<bool, NodeError> AudioExecutor::GetNextFrame(std::span<uint8_t> output_buffer) {
    if (!current_node_) return {false, NodeError::Success};

    // איפוס הבאפר (קריטי למיקסר)
    std::fill(output_buffer.begin(), output_buffer.end(), 0);

    if (auto* audio_node = current_node_->as_audio()) {
        auto result = audio_node->process_frame(output_buffer);

        if (!result) {
            spdlog::info("error detectd!!!!!!!!");
            NodeError err = result.error();

            if (err == NodeError::Underrun) {
                // Underrun אינו קריטי - מחזירים true כדי להמשיך לנגן (שקט)
                return {true, NodeError::Underrun};
            }

            if (err != NodeError::EndOfStream) {
                return {false, err};
            }
            // אם זה EndOfStream, אנחנו ממשיכים למטה ללוגיקת
        }

        bool is_eos = (!result && result.error() == NodeError::EndOfStream);
        bool limit_reached = (current_node_->processed_frames_ >= current_node_->total_frames_);
        if (is_eos || limit_reached) {
            auto result = audio_node->close();
            if (!result) {
                return {false, result.error()};
            }
            spdlog::info("Node [{}] finished. Transitions to [{}]", current_node_->id_,
                         current_node_->target_ ? current_node_->target_->id_ : "END");

            current_node_ = current_node_->target_;

            // אם עברנו לצומת הבא, אנחנו מחזירים true כדי שהלולאה ב-Session תמשיך
            // הפריים הבא יטופל באיטרציה הבאה
            if (current_node_) {
                stats_.current_node_id = current_node_->id_;
            }
        }

        stats_.total_bytes_sent += FRAME_SIZE_BYTES;

        // מחזירים true כל עוד יש צומת פעיל
        return {(current_node_ != nullptr), NodeError::Success};
    }

    // הגענו לצומת שאינו אודיו (כמו ClientsNode בסוף) - מסיימים
    return {false, NodeError::Success};
}
