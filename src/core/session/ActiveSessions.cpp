#include "ActiveSessions.hpp"

#include <spdlog/spdlog.h>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/uuid/uuid.hpp>             // Core UUID class
#include <boost/uuid/uuid_generators.hpp>  // Generators (Random, Name-based)
#include <boost/uuid/uuid_io.hpp>          // Streaming operators (to_string)
#include <stdexcept>

#include "Json2Graph.hpp"
#include "Nodes.hpp"
#include "WebSocketSessionObserver.hpp"

ActiveSessions::ActiveSessions(std::shared_ptr<io_context_pool> pool) : pool_(std::move(pool)) {}

std::string ActiveSessions::create_session(const boost::json::object& jobj) {
    // 1. Safety Check
    if (!pool_) {
        spdlog::critical("ActiveSessions: io_context_pool is null!");
        throw std::runtime_error("Server not initialized: pool_ is null");
    }

    boost::uuids::random_generator generator;
    boost::uuids::uuid uuid = generator();
    
    std::string session_id = boost::uuids::to_string(uuid);
    spdlog::debug("Creating session with ID: {}", session_id);

    // 2. Parse Graph & Create Session
    boost::asio::io_context& io = pool_->get_io_context();
    Graph g = parse_graph(io, jobj);

    spdlog::debug("Graph parsed for session {}. Node count: {}", session_id, g.nodes.size());
    // 3.Create the session
    auto session = std::make_shared<Session>(io, session_id, std::move(g));

    // 4. Register Thread-Safely
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_[session_id] = std::move(session);
        spdlog::debug("Session {} registered.", session_id);
    }

    return session_id;
}

void ActiveSessions::create_and_run_WebsocketSession(const std::string& audio_session_id,
                                                     const req_t& req,
                                                     boost::beast::tcp_stream& stream) {
    spdlog::info("Attaching WebSocket to session {}", audio_session_id);

    std::shared_ptr<Session> session;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(audio_session_id);
        if (it == sessions_.end()) {
            throw std::runtime_error("Session ID not found: " + audio_session_id);
        }
        session = it->second;
    }

    // Handover socket to WebSocketSession
    auto websocket = std::make_shared<WebSocketSession>(stream.release_socket());
    websocket->do_accept(req);

    // Link WebSocket -> Audio Session
    session->AttachObserver(std::make_shared<WebSocketSessionObserver>(websocket));

    // 4. Register Thread-Safely
    {
        std::lock_guard<std::mutex> lock(mutex_);
        websocket_sessions_[audio_session_id] = std::move(websocket);
        spdlog::debug("WebSocketSession {} registered.", audio_session_id);
    }
    // Launch Session Lifecycle
    asio::co_spawn(
        pool_->get_io_context(),
        [this, self = shared_from_this(), id = audio_session_id,
         sess = session]() -> asio::awaitable<void> {
            try {
                co_await sess->start();
            } catch (const std::exception& e) {
                spdlog::error("[{}] Session error: {}", id, e.what());
            }

            // Cleanup on finish
            std::lock_guard<std::mutex> lock(mutex_);
            sessions_.erase(id);
            spdlog::debug("[{}] Session removed.", id);
        },
        asio::detached);
}

// Implementations for lookup methods (get, list_ids) should be added here
// if they were previously missing, following the pattern:
/*
std::shared_ptr<Session> ActiveSessions::get(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(id);
    return (it != sessions_.end()) ? it->second : nullptr;
}
*/
