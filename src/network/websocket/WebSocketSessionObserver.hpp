#pragma once
#include <boost/json.hpp>
#include <string>

#include "ISessionObserver.hpp"
#include "WebSocketSession.hpp"

/**
 * @brief Bridges the C++ Audio Session with the WebSocket Client.
 * * @section Protocol Definition
 * This class pushes JSON events to the client.
 *
 * **Event: Session Statistics** (Sent every ~100ms)
 * @code
 * {
 * "type": "stats",
 * "node": "node_id_123",  // The ID of the node currently playing
 * "progress": 45.5,       // Percentage of current node completed (0.0-100.0)
 * "bytes": 102400         // Total bytes streamed via RTP so far
 * }
 * @endcode
 */
class WebSocketSessionObserver : public ISessionObserver {
    std::weak_ptr<WebSocketSession> ws_;

   public:
    explicit WebSocketSessionObserver(std::shared_ptr<WebSocketSession> ws) : ws_(ws) {}

    void OnStatsUpdate(const SessionStats& stats) override {
        if (auto session = ws_.lock()) {
            boost::json::object j;
            j["type"] = "stats";
            j["node"] = stats.current_node_id;
            j["progress"] = stats.progress_percent;
            j["bytes"] = stats.total_bytes_sent;

            session->Send(boost::json::serialize(j));
        }
    }

    // Implement other methods...
    void OnNodeTransition(const std::string& id) override { /* ... */ }
    void OnSessionComplete() override { /* ... */ }
    void OnError(const std::string& error_message) override { /* ... */ }
};
