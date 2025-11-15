#pragma once
#include <boost/asio.hpp>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <cstdint>
#include <memory>
#include <unordered_map>

#include "Nodes.hpp"
#include "boost/asio/awaitable.hpp"
#include "boost/asio/io_context.hpp"


class Graph;  // Editable graph
namespace net = boost::asio;
class Session : public std::enable_shared_from_this<Session> {
 public:
  Session(boost::asio::io_context &ex, std::string &&Id, Graph &&g);
 net::awaitable<void> start();

 net::awaitable<void> stop();





 private:


  Node *get_start_node();
  net::awaitable<void> do_start();

  net::awaitable<void> do_stop();

  net::awaitable<void> RefillRequest();

  net::awaitable<void> Process_Current_Node_Frame(
      std::span<uint8_t, 160> &frame_buffer);

  net::awaitable<void> Packetize_And_Transmit_Frame(
      std::span<uint8_t, 160> &frame_buffer);

  void getFrame();

  boost::asio::io_context &io_;

  const std::string id_;

  Node *current_node;

  std::unique_ptr<const Graph> graph_;

  net::cancellation_signal stop_signal;

  boost::asio::steady_timer timer_;
  std::array<uint8_t, FRAME_SIZE> frame_data_{0};
};
