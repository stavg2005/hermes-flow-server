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
#include "PacketUtils.hpp"
#include "RTPPacketizer.hpp"
#include "Packet.hpp"

namespace net = boost::asio;
using udp = net::ip::udp;

class RTPStreamer {
   public:
    explicit RTPStreamer(net::io_context& io) : socket_(io, udp::endpoint(udp::v4(), 0)) {
        codec_ = std::make_unique<ALawCodecStrategy>();
        packetizer_ =
            std::make_unique<RTPPacketizer>(codec_->GetPayloadType(), generate_ssrc(),
                                            codec_->GetTimestampIncrement(FRAME_SIZE_BYTES));
    }

    void AddClient(const std::string& ip, uint16_t port) {
        boost::system::error_code ec;
        auto addr = net::ip::make_address(ip, ec);
        if (ec) {
            spdlog::error("Invalid IP: {} - {}", ip, ec.message());
            return;
        }

        udp::endpoint ep(addr, port);

        // Avoid duplicates
        if (std::find(clients_.begin(), clients_.end(), ep) == clients_.end()) {
            clients_.push_back(ep);
            spdlog::info("RTP Client added: {}:{}", ip, port);
        }
    }

    void RemoveClient(const std::string& ip, uint16_t port) {
        try {
            auto addr = net::ip::make_address(ip);
            udp::endpoint ep(addr, port);

            std::erase(clients_, ep);
            spdlog::info("RTP Client removed: {}:{}", ip, port);
        } catch (...) {
        }
    }

    // Zero-copy fan-out.
    // Uses callbacks for parallel dispatch (cheaper than coroutines).
    void SendFrame(std::span<const uint8_t> pcm_frame) {
        if (clients_.empty()) return;

        size_t max_packet_size = PacketUtils::RTP_HEADER_SIZE + pcm_frame.size();
        auto packet_owner = BufferPool::Instance().Acquire(max_packet_size);
        std::span<uint8_t> packet_span(*packet_owner);

        size_t packet_size = PacketUtils::packet2rtp(pcm_frame, *packetizer_, *codec_, packet_span);

        if (packet_size == 0) return;

        for (const auto& endpoint : clients_) {
            socket_.async_send_to(net::buffer(*packet_owner, packet_size), endpoint,
                                  [packet_owner](const boost::system::error_code& ec, std::size_t) {
                                      if (ec) { /* Handle error (e.g., remove client) */
                                      }
                                  });
        }
    }

   private:
    std::unique_ptr<RTPPacketizer> packetizer_;
    std::unique_ptr<ICodecStrategy> codec_;

    udp::socket socket_;
    std::vector<udp::endpoint> clients_;
};
