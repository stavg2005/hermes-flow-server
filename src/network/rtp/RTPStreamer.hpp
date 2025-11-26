#pragma once
#include <memory>
#include <span>
#include <vector>

#include "CodecStrategy.hpp" 
#include "PacketUtils.hpp"
#include "config.hpp"  // for FRAME_SIZE_BYTES
#include "RTPPacketizer.hpp"
#include "RTPTransmitter.hpp"
#include "packet.hpp"

class RTPStreamer {
   public:
    RTPStreamer(boost::asio::io_context& io, std::string dest_ip, uint16_t dest_port)
        : transmitter_(io, dest_ip, dest_port) {
        // 1. Initialize the Strategy (Default to A-Law)
        codec_ = std::make_unique<ALawCodecStrategy>();

        // 2. Initialize Packetizer using the Strategy's metadata
        packetizer_ =
            std::make_unique<RTPPacketizer>(codec_->GetPayloadType(), generate_ssrc(),
                                            codec_->GetTimestampIncrement(FRAME_SIZE_BYTES));
    }

    // Takes RAW PCM, encodes it, packetizes it, and sends it.
    void SendFrame(std::span<const uint8_t> pcm_frame) {
        // 1. Allocate memory that owns itself (Safety for Async Send)
        //    Note: RTP_HEADER_SIZE (12) + Encoded payload size
        //    For A-Law, payload size == pcm_size / 2.
        //    We allocate enough for the worst case (PCM size) just to be safe/easy.
        size_t max_packet_size = PacketUtils::RTP_HEADER_SIZE + pcm_frame.size();
        auto packet_owner = std::make_shared<std::vector<uint8_t>>(max_packet_size);

        std::span<uint8_t> packet_span(*packet_owner);

        // 2. Encode & Packetize using the injected Codec
        //    [New Signature with *codec_]
        size_t packet_size = PacketUtils::packet2rtp(pcm_frame, *packetizer_, *codec_, packet_span);

        // 3. Transmit
        if (packet_size > 0) {
            // asyncSend takes ownership of 'packet_owner' keeping it alive
            transmitter_.asyncSend(std::move(packet_owner), packet_size);
        }
    }

    // Allow switching codecs at runtime if needed
    void SetCodec(std::unique_ptr<ICodecStrategy> new_codec) {
        codec_ = std::move(new_codec);
        // Re-init packetizer with new codec params if necessary,
        // or just update payload type/timestamp in packetizer if supported.
    }

   private:
    RTPTransmitter transmitter_;
    std::unique_ptr<RTPPacketizer> packetizer_;  // Use unique_ptr for delayed init
    std::unique_ptr<ICodecStrategy> codec_;
};
