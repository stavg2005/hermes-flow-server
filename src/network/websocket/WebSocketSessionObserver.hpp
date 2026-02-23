#pragma once
#include <boost/json.hpp>
#include <cstdint>
#include <string>

#include "ISessionObserver.hpp"
#include "Session.hpp"
#include "WebSocketSession.hpp"

using namespace hermes::net::websocket;
using namespace hermes::service;
/**
 * @brief This class pushes JSON events to the client.
 */
class WebSocketSessionObserver : public ISessionObserver {
  std::weak_ptr<WebSocketSession> ws_;

 public:
  explicit WebSocketSessionObserver(std::shared_ptr<WebSocketSession> ws)
      : ws_(ws) {}

  void on_stats_update(const SessionStats& stats) override {
    if (auto session = ws_.lock()) {
      boost::json::object j;
      j["type"] = "stats";
      j["node"] = stats.current_node_id;
      j["bytes"] = stats.total_bytes_sent;
      j["packets"] = stats.packets_sent;
      j["underruns"] = stats.underruns;
      session->send(boost::json::serialize(j));
    }
  }

  void on_webrtc_request(uint16_t& port) override {
    if (auto session = ws_.lock()) {
      boost::json::object response;
      response["type"] = "webrtc_ready";
      response["mountpoint_id"] = port;
      response["message"] =
          "Session created successfully. Watch this mountpoint.";
      session->send(boost::json::serialize(response));
    }
  }

  void on_node_transition(const std::string& id) override { /* ... */ }
  void on_session_complete() override {
    if (auto session = ws_.lock()) {
      boost::json::object j;
      j["type"] = "completion";
      j["message"] = "Session Complete";
      session->send(boost::json::serialize(j));
    }
  }
  void on_error(const std::string& error_message) override {
    if (auto session = ws_.lock()) {
      boost::json::object j;
      j["type"] = "error";
      j["message"] = error_message;
      session->send(boost::json::serialize(j));
    }
  }
};
