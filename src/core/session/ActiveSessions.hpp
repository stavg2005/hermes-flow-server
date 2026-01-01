#pragma once

#include <atomic>
#include <boost/asio/io_context.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http.hpp>
#include <boost/json.hpp>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "Session.hpp"
#include "WebSocketSession.hpp"
#include "io_context_pool.hpp"

/**
 * @brief The Central Hub for all running Audio Workflows.
 * * @details
 * **Responsibilities:**
 * 1. **Registry:** Maintains a thread-safe map of all active sessions (`sessions_`).
 * 2. **Lifecycle Management:** Creates new sessions from JSON configs and ensures
 * they are destroyed properly when execution finishes.
 * 3. **Protocol Bridging:** Acts as the "Glue" layer that allows an HTTP request
 * (WebSocket Upgrade) to find and attach to a running Audio Graph.
 * * **Thread Safety:**
 * Since sessions can be created (HTTP Thread) and destroyed (Worker Thread)
 * concurrently, all internal maps are protected by a `std::mutex`.
 */
class ActiveSessions : public std::enable_shared_from_this<ActiveSessions> {
   public:
    using req_t = http::request<http::string_body>;

    explicit ActiveSessions(std::shared_ptr<io_context_pool> pool);

    /**
     * @brief Factory method to spawn a new Audio Session.
     * * @details
     * 1. Generates a unique UUIDv4 for the session.
     * 2. Parses the JSON payload into an executable `Graph` structure.
     * 3. Allocates the Session object on the Thread Pool.
     * * @param jobj The JSON configuration describing the audio graph nodes and edges.
     * @return The unique session ID (string) to be returned to the client.
     */
    std::string create_session(const boost::json::object& jobj);

    /**
     * @brief Upgrades a connection to WebSocket and links it to audio stats.
     * * @details
     * This method performs a critical "Handover":
     * 1. It locates the running `Session` by ID.
     * 2. It creates a `WebSocketSession` that takes ownership of the TCP socket.
     * 3. It attaches a `WebSocketSessionObserver` to the audio session, allowing
     * real-time stats (progress, bytes sent) to flow back to the client.
     * * @param stream The underlying TCP stream (moved from the HTTP handler).
     */
    void create_and_run_WebsocketSession(const std::string& audio_session_id, const req_t& req,
                                         boost::beast::tcp_stream& stream);

    enum class RemoveStatus {
        Success,           // Found and removed both Audio and WebSocket
        SessionNotFound,   // ID does not exist in Audio map (Critical 404)
        WebSocketNotFound  // Audio removed, but no WebSocket was attached (Usually 200 OK)
    };
    RemoveStatus remove_session(const std::string& id);

    // Lookups
    std::shared_ptr<Session> get(const std::string& id) const;
    std::vector<std::string> list_ids() const;
    std::size_t size() const noexcept;

    // Management
    void stop_all();
    void on_session_stopped(const std::string& id);

    // Disable copy
    ActiveSessions(const ActiveSessions&) = delete;
    ActiveSessions& operator=(const ActiveSessions&) = delete;

   private:
    // mutable allows locking in const methods (like get/size)
    mutable std::mutex mutex_;

    std::shared_ptr<io_context_pool> pool_;
    std::atomic<int64_t> next_session_id_{0};

    // Maps
    std::unordered_map<std::string, std::shared_ptr<Session>> sessions_;
    std::unordered_map<std::string, std::shared_ptr<WebSocketSession>> websocket_sessions_;
};
