#pragma once

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <memory>
#include <span>
#include <vector>

#include "ISessionObserver.hpp"  // For SessionStats
#include "Nodes.hpp"
#include "config.hpp"

/**
 * @brief The "Conductor" of the audio processing pipeline.
 * * Manages the transition from static graph definition to dynamic execution.
 * It is responsible for the two-phase lifecycle:
 * 1. Async Preparation (Download & Buffer).
 * 2. Real-time Execution (Frame processing loop).
 */
class AudioExecutor {
   public:
    /**
     * @brief Constructs the executor with a parsed graph.
     * @param io The IO context for async file operations.
     * @param graph The audio graph structure containing nodes and edges.
     */
    AudioExecutor(boost::asio::io_context& io, std::shared_ptr<Graph> graph);

    /**
     * @brief Accessor for real-time session statistics.
     * @return A reference to the stats object used by Observers (e.g., WebSocket).
     */
    SessionStats& get_stats();

    /**
     * @brief Phase 1: Async Preparation.
     * @details
     * - Scans the graph for `FileInputNode`s.
     * - Triggers S3 downloads for any missing files.
     * - Pre-fills the initial Double Buffers (reads the first ~40ms of audio).
     * * @note This must complete successfully before the real-time loop starts.
     */
    boost::asio::awaitable<void> Prepare();

    /**
     * @brief Phase 2: Real-time Execution.
     * @details
     * This method is designed to be called every 20ms by the Session timer.
     * It pulls data through the graph (Mixer -> Effects -> Output).
     * * @param output_buffer The destination buffer for the mixed PCM audio.
     * @return `true` if a frame was produced, `false` if the graph is exhausted (EOF).
     */
    bool GetNextFrame(std::span<uint8_t> output_buffer);

   private:
    /**
     * @brief Helper to iterate all nodes and ensure assets exist locally.
     * Initiates S3 downloads if files are missing from the disk.
     */
    boost::asio::awaitable<void> FetchFiles();

    /**
     * @brief Configures mixer nodes based on their inputs.
     * Calculates total frame duration for mixers to know when to stop.
     */
    void UpdateMixers();

    boost::asio::io_context& io_;
    std::shared_ptr<Graph> graph_;
    Node* current_node_ = nullptr;
    SessionStats stats_;
};
