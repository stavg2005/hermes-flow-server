#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/asio/awaitable.hpp>
#include <memory>
#include <span>
#include <vector>

#include "Nodes.hpp"
#include "ISessionObserver.hpp" // For SessionStats
#include "config.hpp"

/**
 * @brief Manages the execution flow of the audio graph.
 * Handles file fetching, buffer initialization, and the frame-by-frame processing loop.
 */
class AudioExecutor {
public:
    AudioExecutor(boost::asio::io_context& io, std::shared_ptr<Graph> graph);

    // Get statistics for the current session
    SessionStats& get_stats();

    // 1. Preparation Phase: Downloads files (S3) and initializes buffers
    boost::asio::awaitable<void> Prepare();

    // 2. Execution Phase: Fills a buffer with the next audio frame
    // Returns false if the graph execution is finished
    bool GetNextFrame(std::span<uint8_t> output_buffer);

private:
    // Helpers
    boost::asio::awaitable<void> FetchFiles();
    void UpdateMixers();

    boost::asio::io_context& io_;
    std::shared_ptr<Graph> graph_;
    Node* current_node_ = nullptr;
    SessionStats stats_;
};
