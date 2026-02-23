#pragma once

#include <boost/asio/awaitable.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/io_context.hpp>
#include <memory>
#include <string>

#include "AudioExecutor.hpp"
#include "ISessionObserver.hpp"
#include "RTPStreamer.hpp"
#include "Types.hpp"

using namespace hermes::net::rtp;
using namespace hermes::audio;

namespace hermes::service {
/**
 * @class Session
 * @brief Manages the lifecycle and execution of a specific audio processing
 * graph.
 *
 * The Session class is responsible for:
 * 1. Initializing the AudioExecutor and fetching necessary resources (S3).
 * 2. Running the main 20ms real-time audio loop.
 * 3. Streaming the resulting audio via RTP.
 * 4. Reporting statistics and errors to an attached observer (e.g., WebSocket).
 */

using signal_channel =
    asio::experimental::channel<void(boost::system::error_code)>;
class Session : public std::enable_shared_from_this<Session> {
 public:
  /**
   * @brief Constructs a new Session.
   *
   * @param io The IO context for async operations.
   * @param id A unique identifier for this session.
   * @param g The audio graph to execute (moved into the session).
   */
  Session(boost::asio::io_context& io, std::string id, Graph&& g,
          config::S3Config& s3_config, bool is_web_rtc = false,
          std::string janus_ip = "",
          std::optional<uint16_t> janus_port = std::nullopt);

  /**
   * @brief Destructor.
   * @note Defined in the .cpp file to allow forward declaration of unique_ptr
   * types (Impl pattern).
   */
  ~Session();

  /**
   * @brief Starts the audio graph execution.

   * Calls InitializeGraphExecution() to fetch files and prepare buffers.
   * Configures the streamer.
   * Enters the main loop, ticking every 20ms to process frames.
   */
  boost::asio::awaitable<void> start();

  /**
   * @brief Stops the session execution immediately.
   *
   */
  void stop();

  void pause();

  void resume();

  std::optional<uint16_t> get_webrtc_port() const { return janus_port_; }
  /**
   * @brief Adds a target client for RTP streaming.
   *
   * @param ip The target IP address.
   * @param port The target UDP port.
   */
  void add_client(const std::string& ip, uint16_t port);

  /**
   * @brief Attaches an observer to receive session events.
   *
   * @param observer Shared pointer to an ISessionObserver (e.g.,
   * WebSocketSession).
   */
  void attach_observer(std::unique_ptr<ISessionObserver> observer);

  /**
   * @brief Checks if the session is currently running.
   * @return true if running, false otherwise.
   */
  bool is_running() const;

  /**
   * @brief Scans the graph for 'ClientsNode' and registers them with the
   * RTPStreamer.
   */
  void configure_streamer_from_graph();

  /**
   * @brief Prepares the audio graph for execution asynchronously.
   *
   * Delegates to AudioExecutor::Prepare(), which handles file fetching (S3)
   * and buffer pre-filling.
   *
   * @return true or false if it failed
   */
  boost::asio::awaitable<bool> initialize_graph_execution();

  /**
   * @brief Evaluates the status returned by the AudioExecutor after processing
   * a frame.
   *
   * Handles error logging and reporting to the observer.
   *
   * @param status The NodeError returned from the processing step.
   * @return true ot alse If the session should continue or stop based on the
   * error
   */
  bool is_status_ok(config::NodeErrorCode status);

  /**
   * @brief Sends updated statistics to the observer
   * @param last_stats_time  the timestamp of the last update.
   */
  void update_stats_if_needed(
      std::chrono::steady_clock::time_point& last_stats_time);

  /**
   * @brief Handles the clean shutdown of a session.
   */
  void finalize_session(const config::NodeError& result);

 private:
  asio::io_context& io_;
  std::string id_;
  std::atomic<bool> is_running_{false};
  std::atomic<bool> is_paused_{false};
  bool is_webrtc_{false};
  std::string janus_ip_;
  std::optional<uint16_t> janus_port_;
  std::unique_ptr<Graph> graph_;
  std::unique_ptr<AudioExecutor> audio_executor_;
  std::unique_ptr<RTPStreamer> streamer_;
  signal_channel resume_channel_;
  asio::steady_timer timer_;
  std::unique_ptr<ISessionObserver> observer_;
};
}  // namespace hermes::service
