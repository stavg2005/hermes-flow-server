#pragma once
#include <algorithm>
#include <boost/asio.hpp>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "CodecStrategy.hpp"
#include "Config.hpp"
#include "Packet.hpp"
#include "PacketUtils.hpp"
#include "RTPPacketizer.hpp"
#include "Types.hpp"
#include "spdlog/spdlog.h"

namespace hermes::net::rtp {
namespace asio = boost::asio;
using udp = asio::ip::udp;
class RTPStreamer {
 public:
  explicit RTPStreamer(boost::asio::io_context& io)
      : socket_(io,
                boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 0)) {
    codec_ = std::make_unique<audio::ALawCodecStrategy>();
    packetizer_ = std::make_unique<RTPPacketizer>(
        codec_->get_payload_type(), generate_ssrc(),
        codec_->get_timestamp_increment(config::FRAME_SIZE_BYTES));

    for (int i = 0; i < RING_BUFFER_SIZE; ++i) {

      packet_ring_[i] = std::make_shared<std::vector<uint8_t>>(
          RTP_HEADER_SIZE + config::FRAME_SIZE_BYTES);
    }
  }

  void add_client(const std::string& host_or_ip, uint16_t port) {
    boost::system::error_code ec;
    auto addr = boost::asio::ip::make_address(host_or_ip, ec);
    udp::endpoint ep;

    if (!ec) {
      ep = udp::endpoint(addr, port);
    } else {
      asio::ip::udp::resolver resolver(socket_.get_executor());
      auto results = resolver.resolve(asio::ip::udp::v4(), host_or_ip, std::to_string(port), ec);
      if (ec || results.empty()) {
        spdlog::error("Invalid IP or Hostname: {} - {}", host_or_ip, ec.message());
        return;
      }
      ep = *results.begin();
    }

    if (!std::ranges::contains(clients_, ep)) {
      clients_.push_back(ep);
      spdlog::info("RTP Client added: {}:{}", ep.address().to_string(), port);
    }
  }

  void remove_client(const std::string& host_or_ip, uint16_t port) {
    try {
      boost::system::error_code ec;
      auto addr = asio::ip::make_address(host_or_ip, ec);
      udp::endpoint ep;

      if (!ec) {
        ep = udp::endpoint(addr, port);
      } else {
        asio::ip::udp::resolver resolver(socket_.get_executor());
        auto results = resolver.resolve(asio::ip::udp::v4(), host_or_ip, std::to_string(port), ec);
        if (ec || results.empty()) return;
        ep = *results.begin();
      }

      std::erase(clients_, ep);
      spdlog::info("RTP Client removed: {}:{}", ep.address().to_string(), port);
    } catch (...) {
    }
  }

  // Zero-copy fan-out.
  // Uses callbacks for parallel dispatch (cheaper than coroutines).
  void send_frame(std::span<const uint8_t> pcm_frame) {
    if (clients_.empty()) return;

    auto packet_owner = packet_ring_[ring_index_];

    ring_index_ = (ring_index_ + 1) % RING_BUFFER_SIZE;

    std::span<uint8_t> packet_span(*packet_owner);

    size_t packet_size =
        packet_to_rtp(pcm_frame, *packetizer_, *codec_, packet_span);

    if (packet_size == 0) return;

    for (const auto& endpoint : clients_) {
      socket_.async_send_to(
          asio::buffer(*packet_owner, packet_size), endpoint,
          [this, packet_owner](const boost::system::error_code& ec, std::size_t bytes_transferred) {
            if (!ec) {
               bytes_sent_.fetch_add(bytes_transferred, std::memory_order_relaxed);
               packets_sent_.fetch_add(1, std::memory_order_relaxed);
            } else {
              spdlog::debug("UDP send failed: {}", ec.message());
            }
          });
    }
  }

  uint64_t get_bytes_sent() const { return bytes_sent_.load(std::memory_order_relaxed); }
  uint64_t get_packets_sent() const { return packets_sent_.load(std::memory_order_relaxed); }

 private:
  std::atomic<uint64_t> bytes_sent_{0};
  std::atomic<uint64_t> packets_sent_{0};
  static constexpr int RING_BUFFER_SIZE = 16;
  std::array<std::shared_ptr<std::vector<uint8_t>>, RING_BUFFER_SIZE>
      packet_ring_;
  size_t ring_index_ = 0;
  std::unique_ptr<RTPPacketizer> packetizer_;
  std::unique_ptr<audio::ICodecStrategy> codec_;

  udp::socket socket_;
  std::vector<udp::endpoint> clients_;
};
}  // namespace hermes::net::rtp
