#pragma once
#include <boost/asio/any_io_executor.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Session.hpp"
#include "WebSocketSession.hpp"
#include "boost/asio/io_context.hpp"
#include "boost/beast/core/tcp_stream.hpp"
#include "boost/json.hpp"
#include "io_context_pool.hpp"

using req_t = http::request<http::string_body>;
using res_t = http::response<http::string_body>;
class ActiveSessions : public std::enable_shared_from_this<ActiveSessions> {
   public:
    using Id = std::string;
    explicit ActiveSessions(std::shared_ptr<io_context_pool> pool);

    std::string create_session(const boost::json::object& jobj);

    void create_and_run_WebsocketSession(std::string audio_session_id, const req_t& req,
                                         boost::beast::tcp_stream& stream);
    // Create + run a session; returns the shared_ptr and its id.
    std::string create_and_run_session(const boost::json::object& jobj);

    bool remove_session(const Id& id);

    // Lookup & enumeration
    std::shared_ptr<Session> get(const Id& id) const;
    std::vector<Id> list_ids() const;
    std::size_t size() const noexcept;

    // Management
    void stop_all();  // orderly: async_stop each session
    void on_session_stopped(const Id& id);

    // Non-copyable
    ActiveSessions(const ActiveSessions&) = delete;
    ActiveSessions& operator=(const ActiveSessions&) = delete;

   private:
    Id generate_uuid() const;  // stable key for map
    std::mutex mutex_;
    std::unordered_map<Id, std::shared_ptr<Session>> sessions_;
    std::unordered_map<Id, std::shared_ptr<WebSocketSession>> websocket_sessions_;
    std::shared_ptr<io_context_pool> pool_;
    std::atomic<int64_t> next_session_id_ = 0;
};
