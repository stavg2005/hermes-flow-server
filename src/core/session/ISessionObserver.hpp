#pragma once
#include <string>
namespace hermes::service {
struct SessionStats {
  std::string session_id;
  std::string current_node_id;
  size_t total_bytes_sent;
  size_t packets_sent = 0;
  size_t underruns = 0;
};

/**
 * @brief  Session update interface. Called from the audio thread (must be
 * non-blocking)
 */
struct ISessionObserver {
  virtual ~ISessionObserver() = default;

  virtual void on_stats_update(const SessionStats& stats) = 0;

  virtual void on_webrtc_request(uint16_t& port) = 0;

  virtual void on_node_transition(const std::string& node_id) = 0;

  virtual void on_session_complete() = 0;

  virtual void on_error(const std::string& error_message) = 0;
};
}  // namespace hermes::service
