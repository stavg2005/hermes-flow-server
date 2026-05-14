#pragma once

#include <array>
#include <atomic>
#include <boost/asio.hpp>
#include <memory>
#include <span>
#include <string>
#include <vector>
#include "Config.hpp"
#include "CodecStrategy.hpp"
#include "EncryptionStrategy.hpp"
#include "RTPPacketizer.hpp"

namespace hermes::net::rtp {
namespace asio = boost::asio;
using udp = asio::ip::udp;

/**
 * @struct RtpClientTarget
 * @brief Represents a destination client for the RTP stream.
 */
struct RtpClientTarget {
  /// The UDP endpoint (IP and port) of the client.
  udp::endpoint endpoint;

  /// Simulated packet loss ratio (0.0 to 1.0). Useful for network testing.
  double packet_loss_ratio;

  /**
   * @brief Equality operator for finding or removing clients by endpoint.
   * Allows std::ranges::contains to work with direct endpoint comparisons.
   */
  bool operator==(const udp::endpoint& ep) const { return endpoint == ep; }
};

/**
 * @class RTPStreamer
 * @brief Manages the packetization, optional encryption, and network
 * transmission of audio frames.
 *
 * Utilizes a zero-copy scatter-gather ring buffer to efficiently fan out RTP
 * packets to multiple connected clients asynchronously.
 */
class RTPStreamer {
 public:
  /**
   * @brief Factory method to create an RTPStreamer with dynamic cryptographic
   * keys.
   */
   static std::unique_ptr<RTPStreamer> create(
      boost::asio::io_context& io, const hermes::config::CryptoConfig& crypto_cfg);

  /**
   * @brief Constructs the RTP streamer and initializes the internal buffer
   * ring.
   * @param io The Boost.Asio I/O context for UDP socket operations.
   */
  explicit RTPStreamer(boost::asio::io_context& io,
                       const hermes::config::CryptoConfig& crypto_cfg);

  /**
   * @brief Resolves and adds a new destination client to the multicast stream.
   * @param host_or_ip The hostname or IP address of the client.
   * @param port The target UDP port.
   * @param packet_loss_ratio Optional simulated packet loss ratio (0.0 = no
   * loss).
   */
  void add_client(const std::string& host_or_ip, uint16_t port,
                  double packet_loss_ratio = 0.0);

  /**
   * @brief Removes an existing client from the stream.
   * @param host_or_ip The hostname or IP address of the client.
   * @param port The target UDP port.
   */
  void remove_client(const std::string& host_or_ip, uint16_t port);

  /**
   * @brief Initializes AES-CTR SRTP encryption for the stream.
   * Derives unique cryptographic keys and base IVs using the stream's SSRC.
   */
  void setup_security();

  /**
   * @brief Packetizes, encodes, optionally encrypts, and dispatches an audio
   * frame. Automatically fans out the packet to all registered clients.
   * @param pcm_frame A span containing the raw PCM audio samples.
   */
  void send_frame(std::span<const uint8_t> pcm_frame);

  /**
   * @brief Retrieves the total number of bytes successfully dispatched to the
   * network.
   * @return uint64_t Number of bytes sent.
   */
  uint64_t get_bytes_sent() const {
    return bytes_sent_.load(std::memory_order_relaxed);
  }

  /**
   * @brief Retrieves the total number of packets successfully dispatched to the
   * network.
   * @return uint64_t Number of packets sent.
   */
  uint64_t get_packets_sent() const {
    return packets_sent_.load(std::memory_order_relaxed);
  }

 private:
  /**
   * @brief Derives the base Initialization Vector (IV) using the session SSRC.
   * @param ssrc The Synchronization Source identifier.
   * @return std::vector<uint8_t> The 16-byte derived IV.
   */
  std::vector<uint8_t> derive_iv_from_ssrc(uint32_t ssrc);

  /**
   * @brief Derives the AES session key using HKDF-SHA256 and the session SSRC.
   * @param ssrc The Synchronization Source identifier.
   * @return std::vector<uint8_t> The 16-byte derived session key.
   */
  std::vector<uint8_t> derive_session_key(uint32_t ssrc);

  std::atomic<uint64_t> bytes_sent_{0};
  std::atomic<uint64_t> packets_sent_{0};

  static constexpr int RING_BUFFER_SIZE = 16;
  std::array<std::shared_ptr<std::vector<uint8_t>>, RING_BUFFER_SIZE>
      packet_ring_;
  size_t ring_index_ = 0;

  std::unique_ptr<RTPPacketizer> packetizer_;
  std::unique_ptr<audio::ICodecStrategy> codec_;
  std::unique_ptr<crypto::IEncryptionStrategy> encryptor_;
  std::vector<uint8_t> session_master_key_;
  std::vector<uint8_t> session_salt_;
  udp::socket socket_;
  std::vector<RtpClientTarget> clients_;

  bool encryption_enabled_ = false;
};

}  // namespace hermes::net::rtp
