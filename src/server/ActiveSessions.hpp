#pragma once
#include "Session.hpp"
#include "boost/asio/io_context.hpp"
#include <boost/asio/any_io_executor.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class ActiveSessions : public std::enable_shared_from_this<ActiveSessions> {
public:
  using Id = std::string;

  // Factory: registry itself is always heap-allocated and shared.
  static std::shared_ptr<ActiveSessions> create();

  // Create + run a session; returns the shared_ptr and its id.
  std::string create_and_run_session(Graph graph);

  bool remove_session(const Id &id);

  // Lookup & enumeration
  std::shared_ptr<Session> get(const Id &id) const;
  std::vector<Id> list_ids() const;
  std::size_t size() const noexcept;

  // Management
  void stop_all(); // orderly: async_stop each session
  void GetFrame(); // on 20ms tick everys session must produce a frame
  // Called by Session when it fully stops (to auto-erase)
  void on_session_stopped(const Id &id);

  // Non-copyable
  ActiveSessions(const ActiveSessions &) = delete;
  ActiveSessions &operator=(const ActiveSessions &) = delete;

private:
  explicit ActiveSessions(boost::asio::io_context io);
  Id generate_uuid() const; // stable key for map

  std::unordered_map<Id, std::shared_ptr<Session>> sessions_;
};
