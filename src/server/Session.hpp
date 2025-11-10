#pragma once

#include "boost/asio/io_context.hpp"

#include <atomic>

#include <boost/asio/any_io_executor.hpp>

#include <boost/asio/experimental/channel.hpp>

#include <boost/asio/steady_timer.hpp>

#include <cstdint>
#include <memory>

#include <optional>

#include <string_view>
#include <vector>

class TransmissionJob; // RTP/Janus fanout

class WebSocketController; // Stats + control WS

class Graph; // Editable graph

class GraphPlan; // Compiled, immutable DAG

class Session : public std::enable_shared_from_this<Session> {

public:
  using SessionId = std::uint64_t;

  // Factory to ensure shared_ptr from the start

  static std::shared_ptr<Session> create(boost::asio::io_context ex,
                                         SessionId id);

  // Non-blocking; schedules startup on executor.

  void InitilizeBuffers();

  void start();

  void stop();

  SessionId id() const noexcept { return id_; }

  void set_graph(std::shared_ptr<Graph> g); // authoring graph

  // Disallow copy/move to keep ownership simple

  Session(const Session &) = delete;

  Session &operator=(const Session &) = delete;

  Session(Session &&) = delete;

  Session &operator=(Session &&) = delete;

  ~Session();

private:
  Session(boost::asio::io_context &ex, SessionId id);

  void do_start();

  void do_stop(std::function<void()> on_complete = {});

  void RefillRequest();

  void getFrame();
  // Executor affinity for all async ops & state changes

  boost::asio::io_context &io;

  const SessionId id_;

  std::string current_node_id;

  std::vector<uint8_t> files_buffers;

  std::unique_ptr<WebSocketController> websocket_;

  std::shared_ptr<const Graph> graph_;

  boost::asio::steady_timer timer_;
};