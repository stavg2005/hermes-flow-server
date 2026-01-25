#include "core/graph/AudioExecutor.hpp"

#include <algorithm>
#include <expected>
#include <filesystem>
#include <vector>

#include "FileSink.hpp"
#include "Nodes.hpp"
#include "core/graph/Node.hpp"
#include "network/s3/S3Session.hpp"
#include "spdlog/spdlog.h"

// Using namespaces in .cpp is fine (but never in .hpp!)
using namespace hermes::net::s3;
using namespace hermes::service;

namespace hermes::audio {

AudioExecutor::AudioExecutor(boost::asio::io_context& io,
                             std::shared_ptr<Graph> graph)
    : io_(io), graph_(std::move(graph)) {
  if (!graph_ || !graph_->start_node.lock() ) {
    throw std::runtime_error("Invalid graph: missing start node.");
  }
  current_node_ = graph_->start_node.lock();
}

SessionStats& AudioExecutor::GetStats() { return stats_; }

boost::asio::awaitable<std::expected<void, config::ErrorInfo>>
AudioExecutor::Prepare() {
  spdlog::info("Preparing Audio Graph...");

  if (!current_node_) {
    co_return Error(config::AppError::LogicError,
                    "Invalid Graph: No start node");
  }

  // 1. Ensure all physical assets exist on disk (S3 Download)
  auto fetch_res = co_await EnsureAssetsExist();
  if (!fetch_res) {
    co_return std::unexpected(fetch_res.error());
  }

  // 2. Initialize buffers for any node that needs async setup
  //    (Now handled generically via IAsyncInitializer interface)
  auto init_res = co_await InitializeNodes();
  if (!init_res) {
    co_return std::unexpected(init_res.error());
  }

  // 3. Configure Mixers
  UpdateMixers();

  // 4. Reset Stats
  current_node_ = graph_->start_node.lock();
  stats_.current_node_id = current_node_->Id();
  stats_.total_bytes_sent = 0;

  co_return std::expected<void, config::ErrorInfo>();
}

boost::asio::awaitable<std::expected<void, config::ErrorInfo>>
AudioExecutor::EnsureAssetsExist() {
  spdlog::info("Checking asset availability...");

  // Optimization: Hold the session here so we reuse it across multiple files
  std::shared_ptr<net::s3::S3Session> s3_session = nullptr;

  for (const auto& node : graph_->nodes) {
    // 1. Filter Logic (Cleaner early exit)
    if (node->Kind() != NodeKind::FileInput) {
      continue;
    }

    auto* file_node = static_cast<FileInputNode*>(node.get());

    if (std::filesystem::exists(file_node->file_path_)) {
      continue;
    }

    spdlog::info("File missing: {}. Requesting from S3...",
                 file_node->file_name_);

    if (!s3_session) {
      auto session_result = net::s3::S3Session::Create(io_);
      if (!session_result) {
        co_return std::unexpected(config::ErrorInfo::From(
            config::AppError::NetworkError,
            "Failed to create S3Session: " + session_result.error().message));
      }
      s3_session = std::move(*session_result);
    }

    infra::FileSink sink(io_);

    if (auto res = sink.Prepare(file_node->file_path_); !res) {
      co_return std::unexpected(config::ErrorInfo::From(
          config::AppError::FileSystemError,
          "Failed to prepare file sink: " +
              res.error()  // Assuming string error
          ));
    }

    // 5. Download
    auto download_result =
        co_await s3_session->RequestFile(file_node->file_name_, sink);

    if (!download_result) {
      co_return std::unexpected(config::ErrorInfo::From(
          config::AppError::NetworkError,
          "S3 Download failed: " + download_result.error().message));
    }

    sink.Commit();

    spdlog::info("Successfully downloaded: {}", file_node->file_name_);
  }

  co_return std::expected<void, config::ErrorInfo>();
}

boost::asio::awaitable<std::expected<void, config::ErrorInfo>>
AudioExecutor::InitializeNodes() {
  spdlog::info("Initializing async nodes...");

  for (const auto& node : graph_->nodes) {
    // Check if this node implements IAsyncInitializer (e.g. AsyncAudioSource)
    if (auto* async_node = dynamic_cast<IAsyncInitializer*>(node.get())) {
      spdlog::debug("Initializing buffers for node [{}]", node->Id());
      co_await async_node->InitializeBuffers();
    }
  }
  co_return std::expected<void, config::ErrorInfo>();
}

void AudioExecutor::UpdateMixers() {
  for (const auto& node : graph_->nodes) {
    if (node->Kind() == NodeKind::Mixer) {
      if (auto* mixer = dynamic_cast<MixerNode*>(node.get())) {
        mixer->SetMaxFrames();
      }
    }
  }
}

std::pair<bool, config::NodeError> AudioExecutor::GetNextFrame(
    std::span<uint8_t> output_buffer) {
  if (current_node_ != nullptr) {
    return {false, config::NodeError{config::NodeErrorCode::Success, "", ""}};
  }

  // Clear buffer before processing
  std::fill(output_buffer.begin(), output_buffer.end(), 0);

  if (auto* audio_node = current_node_->AsAudio()) {
    auto result = audio_node->ProcessFrame(output_buffer);

    if (!result) {
      config::NodeError err = result.error();

      // Non-critical error (Underrun) -> Log & Continue
      if (err.code == config::NodeErrorCode::Underrun) {
        return {true, err};
      }

      // Critical error that isn't just "Stream Over"
      if (err.code != config::NodeErrorCode::EndOfStream) {
        return {false, err};
      }
    }

    // Handle Natural End (EOS) or Frame Limit
    bool is_eos =
        (!result && result.error().code == config::NodeErrorCode::EndOfStream);

    ;

    if (is_eos || current_node_->IsComplete()) {
      auto close_result = audio_node->Close();
      if (!close_result) {
        return {false, close_result.error()};
      }

      spdlog::info(
          "Node [{}] finished. Transitioning to [{}]", current_node_->Id(),
          current_node_->Next() != nullptr ? current_node_->Next()->Id()
                                           : "END");

      current_node_ = current_node_->Next();

      if (current_node_ != nullptr) {
        stats_.current_node_id = current_node_->Id();
      }
    }

    stats_.total_bytes_sent += hermes::config::FRAME_SIZE_BYTES;

    // Return true if we still have a valid node to process next tick
    return {(current_node_ != nullptr),
            config::NodeError{config::NodeErrorCode::Success, "", ""}};
  }

  // Current node is not an audio processor (e.g. Logic node), stop.
  return {false, config::NodeError{config::NodeErrorCode::Success, "", ""}};
}

}  // namespace hermes::audio
