#include "Session.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <chrono>
#include <memory>
#include <variant>

#include "../models/Nodes.hpp"
#include "boost/asio/awaitable.hpp"
#include "boost/asio/io_context.hpp"
#include "boost/asio/random_access_file.hpp"
#include "models/Nodes.hpp"
using namespace std::chrono_literals;
namespace net = boost::asio;

Session::Session(boost::asio::io_context& ex, std::string id, Graph& g)
    : io_(ex),
      id_(id),
      graph_(std::make_unique<Graph>(std::move(g))),
      timer_(ex, 20ms),
      current_node(get_start_node()){

      };

void Session::run() {
  net::co_spawn(
      io_, [this, self = shared_from_this()]() { return do_start(); },
      net::detached);
}

net::awaitable<void> Session::do_start() {
  InitilizeBuffers();
  auto current_node = get_start_node();
  try {
    for (;;) {
      auto cycle_start_time = std::chrono::steady_clock::now();

      std::span<uint8_t, FRAME_SIZE> frame_buffer_(frame_data_.data(),
                                                   FRAME_SIZE);
      // process the frame into the buffer
      co_await Process_Current_Node_Frame(frame_buffer_);

      co_await Packetize_And_Transmit_Frame(frame_buffer_);

      auto work_duration = std::chrono::steady_clock::now() - cycle_start_time;

      auto sleep_duration = 20ms - work_duration;

      if (sleep_duration.count() > 0) {
        timer_.expires_after(sleep_duration);

        co_await timer_.async_wait(net::use_awaitable);
      }
    }
  } catch (const boost::system::system_error& e) {
    if (e.code() == net::error::operation_aborted) {
    } else {
      // This was a real network or file error
    }
  }
}

net::awaitable<void> Session::Process_Current_Node_Frame(
    std::span<uint8_t, 160>& frame_buffer) {
  current_node->ProcessFrame(frame_buffer);
  current_node->processed_frames += FRAME_SIZE;

  // Check if this frame was the last one for this node.
  if (current_node->processed_frames == current_node->total_frames) {
    current_node = current_node->target;
    co_return;
  }

  if (current_node->processed_frames >= REFILL_THRESHOLD) {
    co_await RefillRequest();

    current_node->processed_frames = 0;
  }

  co_return;
}
