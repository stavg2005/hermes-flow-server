#include "RTPPacketizer.hpp"

#include <cstdint>
#include <ctime>
#include <packet.hpp>

#include "boost/core/span.hpp"

RTPPacketizer::RTPPacketizer(uint8_t payload_type, uint32_t ssrc, uint32_t timestamp_increment)
    : payload_type_(payload_type), ssrc_(ssrc), timestamp_increment_(timestamp_increment) {}

size_t RTPPacketizer::packetize(boost::span<uint8_t> payload, boost::span<uint8_t> out_buffer) {
    RTPPacket::Header hdr{/* padding */ false,
                          version_,
                          /* payload_type */ payload_type_,
                          /* marker */ false,
                          sequence_num_++,
                          timestamp_,
                          ssrc_,
                          /* csrc_list */ {},
                          /* extension */ std::nullopt};

    RTPPacket packet{std::move(hdr), payload};

    auto maybe_span = packet.to_buffer(out_buffer);

    if (!maybe_span) {
        return 0;
    }

    timestamp_ += timestamp_increment_;
    return maybe_span->size();
}

void RTPPacketizer::update_timestamp() {
    timestamp_ += timestamp_increment_;
}

uint32_t RTPPacketizer::current_timestamp() const {
    return timestamp_;
}
