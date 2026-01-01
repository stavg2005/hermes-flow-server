#pragma once
#include <string>

struct SessionStats {
    std::string session_id;
    std::string current_node_id; 
    double progress_percent;
    size_t total_bytes_sent;
    int active_inputs;
    double buffer_health;
};

/**
 * @brief  Session update interface. Called from the audio thread (must be non-blocking)
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
