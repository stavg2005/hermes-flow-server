#pragma once

#include <atomic>
#include <boost/asio/io_context.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http.hpp>
#include <boost/json.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "IoContextPool.hpp"
#include "Session.hpp"
#include "WebSocketSession.hpp"


/**
 * @brief manages session lifecycle and websocket association.
 */
class ActiveSessions : public std::enable_shared_from_this<ActiveSessions> {
   public:
    using req_t = http::request<http::string_body>;

    explicit ActiveSessions(std::shared_ptr<IoContextPool> pool);

    /**
     * @brief Factory method to spawn a new Audio Session.
     * @return The unique session ID (string) to be returned to the client.
     */
    std::string create_session(const boost::json::object& jobj);

    /**
     * @brief Upgrades connection to WebSocket and attaches observer to the audio session.
     * @param stream The underlying TCP stream (moved from the HTTP handler).
     */
    void create_and_run_WebsocketSession(const std::string& audio_session_id, const req_t& req,
                                         boost::beast::tcp_stream& stream);

    enum class RemoveStatus {
        Success,          
        SessionNotFound,
        WebSocketNotFound
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
    mutable std::mutex mutex_;

    std::shared_ptr<IoContextPool> pool_;
    std::atomic<int64_t> next_session_id_{0};
    boost::uuids::random_generator generator_;

    // Maps
    std::unordered_map<std::string, std::shared_ptr<Session>> sessions_;
    std::unordered_map<std::string, std::shared_ptr<WebSocketSession>> websocket_sessions_;
};
