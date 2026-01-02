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

    virtual ~ISessionObserver() = default;


    virtual void OnStatsUpdate(const SessionStats& stats) = 0;


    virtual void OnNodeTransition(const std::string& node_id) = 0;


    virtual void OnSessionComplete() = 0;


    virtual void OnError(const std::string& error_message) = 0;
};
