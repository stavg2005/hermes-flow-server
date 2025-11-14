#include "ActiveSessions.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <memory>

#include "../utils/Json2Graph.hpp"
#include "Session.hpp"
#include "boost/asio/io_context.hpp"
#include "io_context_pool.hpp"
#include "models/Nodes.hpp"
#include "spdlog/spdlog.h"

namespace net = boost::asio;
ActiveSessions::ActiveSessions(std::shared_ptr<io_context_pool> pool)
    : pool_(std::move(pool)) {};

std::string ActiveSessions::create_and_run_session(const bj::object& jobj) {
  std::string key = std::to_string(next_session_id_++);
  std::string id_for_session = key;
  Graph g = parse_graph(jobj);
  spdlog::debug("Graph for session {} has {} nodes.", id_for_session,
                g.nodes.size());
  if (!pool_) {
    spdlog::critical(
        "ActiveSessions: io_context_pool is null! Cannot create session.");
    // We can't continue.
    throw std::runtime_error("Server is not initialized: pool_ is null");
  }
  spdlog::debug("creating session with id {}", id_for_session);
  boost::asio::io_context& io = pool_->get_io_context();
  spdlog::debug("In active session fetched io context for  session {}",
                id_for_session);
  auto session =
      std::make_unique<Session>(io, std::move(id_for_session), std::move(g));
  spdlog::debug("Session {} Created", id_for_session);
  // Get the raw pointer *before* moving the unique_ptr
  Session* session_ptr = session.get();

  // --- LOCK THE MUTEX for the write ---
  {
    std::lock_guard<std::mutex> lock(mutex_);

    sessions_[key] = std::move(session);
    spdlog::debug("Session with ID {} has been registerd", id_for_session);
  }
  // --- Mutex is unlocked here ---

  net::co_spawn(
      io,
      // FIX 2: Add the coroutine return type to the lambda
      [this, self = shared_from_this(), key,
       session_ptr]() -> net::awaitable<void> {
        try {
          // This now correctly co_awaits the public start() coroutine
          co_await session_ptr->start();
        } catch (const std::exception& e) {
          spdlog::error("Error has occured in Session {}", e.what());
        }

        // Now that co_await is finished, the session is over.
        // Clean it up.
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_.erase(key);
      },
      net::detached);

  return key;
};
