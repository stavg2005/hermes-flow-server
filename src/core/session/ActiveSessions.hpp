#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http.hpp>
#include <boost/json.hpp>

#include "Session.hpp"
#include "WebSocketSession.hpp"
#include "io_context_pool.hpp"



class ActiveSessions : public std::enable_shared_from_this<ActiveSessions> {
public:
    using req_t = http::request<http::string_body>;

    explicit ActiveSessions(std::shared_ptr<io_context_pool> pool);

    /**
     * @brief Parses the JSON graph, creates a Session, and registers it.
     * @return The unique Session ID.
     */
    std::string create_session(const boost::json::object& jobj);

    /**
     * @brief Promotes an HTTP connection to a WebSocket and attaches it to an existing audio session.
     */
    void create_and_run_WebsocketSession(std::string audio_session_id,
                                         const req_t& req,
                                         boost::beast::tcp_stream& stream);

    bool remove_session(const std::string& id);

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
