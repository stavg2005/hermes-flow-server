#include "ActiveSessions.hpp"

#include <spdlog/spdlog.h>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>

#include <stdexcept>

#include "Json2Graph.hpp"
#include "Nodes.hpp"
#include "WebSocketSessionObserver.hpp"

ActiveSessions::ActiveSessions(std::shared_ptr<IoContextPool> pool) : pool_(std::move(pool)) {}

std::string ActiveSessions::create_session(const boost::json::object& jobj) {
    if (!pool_) {
        spdlog::critical("ActiveSessions: IoContextPool is null!");
        throw std::runtime_error("Server not initialized: pool_ is null");
    }


    boost::uuids::uuid uuid = generator_();

    std::string session_id = boost::uuids::to_string(uuid);
    spdlog::debug("Creating session with ID: {}", session_id);

    boost::asio::io_context& io = pool_->get_io_context();
    Graph g = parse_graph(io, jobj);

    spdlog::debug("Graph parsed for session {}. Node count: {}", session_id, g.nodes.size());

    auto session = std::make_shared<Session>(io, session_id, std::move(g));

    // Register Thread-Safely
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

    if (session->get_is_running()) {
        throw std::runtime_error("a websocket is already connected to this session");
    }

    auto websocket = std::make_shared<WebSocketSession>(stream.release_socket());
    websocket->do_accept(req);

    session->AttachObserver(std::make_shared<WebSocketSessionObserver>(websocket));

    {
        std::lock_guard<std::mutex> lock(mutex_);
        websocket_sessions_[audio_session_id] = std::move(websocket);
        spdlog::debug("WebSocketSession {} registered.", audio_session_id);
    }

    asio::co_spawn(
        pool_->get_io_context(),
        [this, self = shared_from_this(), id = audio_session_id,
         sess = session]() -> asio::awaitable<void> {
            try {
                co_await sess->start();
            } catch (const std::exception& e) {
                spdlog::error("[{}] Session error: {}", id, e.what());
            }

            remove_session(id);
        },
        asio::detached);
}

ActiveSessions::RemoveStatus ActiveSessions::remove_session(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto session_it = sessions_.find(id);
    if (session_it == sessions_.end()) {
        return RemoveStatus::SessionNotFound;
    }

    session_it->second->stop();
    sessions_.erase(session_it);
    spdlog::info("[{}] Audio session stopped and removed.", id);

    auto ws_it = websocket_sessions_.find(id);
    if (ws_it != websocket_sessions_.end()) {
        ws_it->second->close();
        websocket_sessions_.erase(ws_it);
        spdlog::info("[{}] WebSocket session detached and closed.", id);

        return RemoveStatus::Success;
    }

    // Audio was removed, but WS wasn't there.
    // This is technically a success, but we report the detail.
    spdlog::warn("[{}] Audio removed, but WebSocket was missing.", id);
    return RemoveStatus::WebSocketNotFound;
}
