#pragma once
#include <boost/json.hpp>
#include <string>

#include "ISessionObserver.hpp"
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

  void OnStatsUpdate(const SessionStats& stats) override {
    if (auto session = ws_.lock()) {
      boost::json::object j;
      j["type"] = "stats";
      j["node"] = stats.current_node_id;
      j["bytes"] = stats.total_bytes_sent;

      session->send(boost::json::serialize(j));
    }
  }

  void OnNodeTransition(const std::string& id) override { /* ... */ }
  void OnSessionComplete() override {
    if (auto session = ws_.lock()) {
      boost::json::object j;
      j["type"] = "completion";
      j["message"] = "Session Complete";
      session->send(boost::json::serialize(j));
    }
  }
  void OnError(const std::string& error_message) override {
    if (auto session = ws_.lock()) {
      boost::json::object j;
      j["type"] = "error";
      j["message"] = error_message;
      session->send(boost::json::serialize(j));
    }
  }
};
