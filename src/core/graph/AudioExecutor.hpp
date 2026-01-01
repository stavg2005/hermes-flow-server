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
 * @brief Executes the audio graph. Handles asset loading and the main processing loop
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
     * @return A reference to the stats object used by Observers (e.g., WebSocket).
     */
    SessionStats& get_stats();

    /**
     * @brief Scans the graph for FileInputNodes.
     * Triggers S3 downloads for any missing files.
     * Pre-fills the initial Double Buffers.
     */
    boost::asio::awaitable<void> Prepare();

    /**
     * @brief pull data from the current node that is being proccesed
     * @param output_buffer buffer for the mixed PCM audio.
     * @return true if a frame was produced, false if the graph is over .
     */
    bool GetNextFrame(std::span<uint8_t> output_buffer);

   private:
    /**
     * @brief Helper to iterate all nodes and ensure files exist locally.
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
