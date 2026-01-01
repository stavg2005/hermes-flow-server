#pragma once
#include <algorithm>
#include <boost/asio.hpp>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "spdlog/spdlog.h"

// Core/Infra includes
#include "CodecStrategy.hpp"  // Adjust path if needed
#include "alaw.hpp"
#include "config.hpp"

// Local RTP includes
#include "PacketUtils.hpp"
#include "RTPPacketizer.hpp"
#include "packet.hpp"

namespace net = boost::asio;
using udp = net::ip::udp;

class RTPStreamer {
   public:
    // 1. Constructor: Open socket on ANY available port
    explicit RTPStreamer(net::io_context& io) : socket_(io, udp::endpoint(udp::v4(), 0)) {
        codec_ = std::make_unique<ALawCodecStrategy>();
        packetizer_ =
            std::make_unique<RTPPacketizer>(codec_->GetPayloadType(), generate_ssrc(),
                                            codec_->GetTimestampIncrement(FRAME_SIZE_BYTES));
    }

    // 2. Client Management (Called by Session when users join/leave)
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

            std::erase(clients_, ep);  // Requires C++20
            spdlog::info("RTP Client removed: {}:{}", ip, port);
        } catch (...) {
        }
    }

    /* --------------------------------------------------------------------------
     * Optimization: Zero-Copy Fan-Out
     * --------------------------------------------------------------------------
     * We allocate the RTP packet ONCE on the heap (packet_owner).
     * * Critical: We capture 'packet_owner' by VALUE in the lambda.
     * [packet_owner](...) { ... }
     *
     * This increases the shared_ptr ref-count. The memory remains valid until
     * the LAST client has finished sending. This allows us to broadcast to
     * 1000+ clients with only 1 memory allocation and 1 memcopy (PCM->RTP).
     */
    void SendFrame(std::span<const uint8_t> pcm_frame) {
        // Optimization: Don't do work if no one is listening
        {
            if (clients_.empty()) return;
        }

        // A. Allocate & Encode ONCE
        //    We create ONE shared buffer for the packet.
        size_t max_packet_size = PacketUtils::RTP_HEADER_SIZE + pcm_frame.size();
        auto packet_owner = std::make_shared<std::vector<uint8_t>>(max_packet_size);
        std::span<uint8_t> packet_span(*packet_owner);

        size_t packet_size = PacketUtils::packet2rtp(pcm_frame, *packetizer_, *codec_, packet_span);

        if (packet_size == 0) return;

        // B. Send the SAME buffer to ALL clients
        //    This is "Zero-Copy" broadcasting.

        for (const auto& endpoint : clients_) {
            /* * DESIGN DECISION: Callbacks vs Coroutines
             * ----------------------------------------
             * We intentionally use a raw Callback here instead of co_await.
             * * 1. Parallel Dispatch (Low Latency):
             * 'async_send_to' returns immediately, allowing us to hand off
             * packets to the OS for ALL clients in microseconds.
             * If we used 'co_await' in this loop, it would serialize the sends
             * (Client 2 waits for Client 1), causing massive latency jitter
             * for the last clients in the list.
             * * 2. Low Overhead (High Throughput):
             * Using 'asio::co_spawn' per client would allocate a new coroutine
             * frame (heap alloc) every 20ms per client, thrashing the allocator.
             * This callback method is effectively allocation-free (aside from
             * the internal Asio handler).
             */
            socket_.async_send_to(
                net::buffer(*packet_owner, packet_size), endpoint,
                // Capture shared_ptr by value to keep memory alive until send completes
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
