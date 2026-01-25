#pragma once
#include <algorithm>
#include <boost/asio.hpp>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "BufferPool.hpp"
#include "spdlog/spdlog.h"

// Core/Infra includes
#include "CodecStrategy.hpp"
#include "Config.hpp"

// Local RTP includes
#include "Packet.hpp"
#include "PacketUtils.hpp"
#include "RTPPacketizer.hpp"
#include "Types.hpp"

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
  }

  void add_client(const std::string& ip, uint16_t port) {
    boost::system::error_code ec;
    auto addr = boost::asio::ip::make_address(ip, ec);
    if (ec) {
      spdlog::error("Invalid IP: {} - {}", ip, ec.message());
      return;
    }

    udp::endpoint ep(addr, port);

    if (!std::ranges::contains(clients_, ep)) {
      clients_.push_back(ep);
      spdlog::info("RTP Client added: {}:{}", ip, port);
    }
  }

  void remove_client(const std::string& ip, uint16_t port) {
    try {
      auto addr = asio::ip::make_address(ip);
      udp::endpoint ep(addr, port);

      std::erase(clients_, ep);
      spdlog::info("RTP Client removed: {}:{}", ip, port);
    } catch (...) {
    }
  }

  // Zero-copy fan-out.
  // Uses callbacks for parallel dispatch (cheaper than coroutines).
  void send_frame(std::span<const uint8_t> pcm_frame) {
    if (clients_.empty()) return;

    size_t max_packet_size = RTP_HEADER_SIZE + pcm_frame.size();
    auto packet_owner =
        infra::BufferPool::instance().acquire(max_packet_size);
    std::span<uint8_t> packet_span(*packet_owner);

    size_t packet_size =
        packet_to_rtp(pcm_frame, *packetizer_, *codec_, packet_span);


    if (packet_size == 0) return;

    for (const auto& endpoint : clients_) {
      socket_.async_send_to(
          asio::buffer(*packet_owner, packet_size), endpoint,
          [packet_owner](const boost::system::error_code& ec, std::size_t) {
            if (ec) { /* Handle error (e.g., remove client) */
            }
          });
    }
  }

 private:
  std::unique_ptr<RTPPacketizer> packetizer_;
  std::unique_ptr<audio::ICodecStrategy> codec_;

  udp::socket socket_;
  std::vector<udp::endpoint> clients_;
};
}  // namespace hermes::net::rtp
