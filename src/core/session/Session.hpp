#pragma once

#include <boost/asio/awaitable.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/io_context.hpp>
#include <memory>
#include <string>

#include "AudioExecutor.hpp"
#include "Config.hpp"
#include "ISessionObserver.hpp"
#include "RTPStreamer.hpp"
#include "Types.hpp"

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
    boost::asio::experimental::channel<void(boost::system::error_code)>;
class Session : public std::enable_shared_from_this<Session> {
 public:
  static std::expected<std::shared_ptr<Session>, config::ErrorInfo> create(
      boost::asio::io_context& io, std::string id, audio::Graph&& g,
      const config::S3Config& s3_config,
      const config::CryptoConfig& crypto_config, bool is_web_rtc,
      std::string janus_ip, std::optional<uint16_t> janus_port,
      bool is_encrypted);

  /**
   * @brief Constructs a new Session.
   *
   * @param io The IO context for async operations.
   * @param id A unique identifier for this session.
   * @param g The audio graph to execute (moved into the session).
   */
  Session(boost::asio::io_context& io, std::string id,
          std::unique_ptr<audio::Graph> g,
          std::unique_ptr<audio::AudioExecutor> audio_executor,std::unique_ptr<net::rtp::RTPStreamer> streamer, bool is_web_rtc,
          std::string janus_ip, std::optional<uint16_t> janus_port,
          bool is_encrypted);

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
  uint64_t get_rtp_bytes_sent() const;
  uint64_t get_rtp_packets_sent() const;
  std::string get_id() const { return id_; }
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

 private:
  boost::asio::io_context& io_;
  std::string id_;
  std::atomic<bool> is_running_{false};
  std::atomic<bool> is_paused_{false};
  bool is_webrtc_{false};
  std::string janus_ip_;
  std::optional<uint16_t> janus_port_;
  std::unique_ptr<audio::Graph> graph_;
  std::unique_ptr<audio::AudioExecutor> audio_executor_;
  std::unique_ptr<net::rtp::RTPStreamer> streamer_;
  signal_channel resume_channel_;
  boost::asio::steady_timer timer_;
  std::unique_ptr<ISessionObserver> observer_;

  /**
   * @brief Scans the graph for 'ClientsNode' and registers them with the
   * RTPStreamer.
   */
  void configure_streamer_from_graph();

  /**
   * @brief Prepares the audio graph for execution asynchronously.
   * Delegates to AudioExecutor::prepare(), which handles S3 fetching and
   * buffer pre-filling.
   * @return true on success, false on failure.
   */
  boost::asio::awaitable<bool> initialize_graph_execution();

  /**
   * @brief Evaluates the status returned by the AudioExecutor after a frame.
   * @return true if the session should continue, false on critical error.
   */
  bool is_status_ok(config::NodeErrorCode code);

  /**
   * @brief Sends updated statistics to the observer if interval has elapsed.
   */
  void update_stats_if_needed(
      std::chrono::steady_clock::time_point& last_stats_time);

  /**
   * @brief Handles session teardown; notifies the observer of the final result.
   */
  void finalize_session(const config::NodeError& result);

  /**
   * @brief The core audio loop (AUDIO_TICK_INTERVAL ticks).
   */
  boost::asio::awaitable<std::expected<void, config::NodeError>>
  run_main_audio_loop();

  /**
   * @brief Suspends the coroutine until the resume channel receives a signal.
   */
  boost::asio::awaitable<void> wait_for_resume(
      std::chrono::steady_clock::time_point& next_tick,
      std::chrono::steady_clock::time_point& last_stats_time);

  /**
   * @brief Waits for the next 20ms tick.
   * @return false if the timer was aborted (session stopping), true otherwise.
   */
  boost::asio::awaitable<std::expected<void, config::NodeError>>
  wait_for_next_tick(std::chrono::steady_clock::time_point& next_tick);

  /**
   * @brief Fetches a frame from the executor and dispatches it.
   * @return false if the executor signaled the end of the stream.
   */
  std::expected<void, config::NodeError> process_and_stream_single_frame(
      std::span<uint8_t> pcm_buffer,
      std::chrono::steady_clock::time_point& last_stats_time);
};
}  // namespace hermes::service
