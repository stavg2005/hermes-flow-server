#pragma once
#include <string>

struct SessionStats {
    std::string session_id;
    std::string current_node_id;  // Which node is currently processing?
    double progress_percent;      // 0.0 to 100.0
    size_t total_bytes_sent;      // For network graph
    int active_inputs;            // How many files are mixing right now
    double buffer_health;         // Optional: 0.0 (Empty) to 1.0 (Full)
};

/**
 * @brief Contract for receiving real-time session updates.
 * * @details
 * **Pattern:** Observer / Listener.
 * **Thread Safety:** These methods are called directly from the Audio Thread.
 * Implementations must be fast and non-blocking (e.g., posting to another
 * thread or updating atomic counters) to avoid audio glitches.
 */
struct ISessionObserver {
    // Virtual Destructor (Crucial for Interfaces)
    virtual ~ISessionObserver() = default;

    // The Core Update Loop
    //    Called periodically (e.g., every 100ms or every frame)
    virtual void OnStatsUpdate(const SessionStats& stats) = 0;

    // Lifecycle Events
    //    Called when the graph moves to a new node (e.g., "Intro" -> "Chorus")
    virtual void OnNodeTransition(const std::string& node_id) = 0;

    //    Called when the session finishes successfully
    virtual void OnSessionComplete() = 0;

    //    Called on critical failure (e.g., File not found)
    virtual void OnError(const std::string& error_message) = 0;
};
