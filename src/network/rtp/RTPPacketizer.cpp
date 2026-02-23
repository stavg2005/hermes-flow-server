#include "RTPPacketizer.hpp"

#include <Packet.hpp>
#include <cstdint>
#include <ctime>
#include <random>

#include "boost/core/span.hpp"
namespace hermes::net::rtp {
RTPPacketizer::RTPPacketizer(uint8_t payload_type, uint32_t ssrc,
                             uint32_t timestamp_increment)
    : payload_type_(payload_type),
      ssrc_(ssrc),
      timestamp_increment_(timestamp_increment) {
  //since we also support webrtc 
  // RFC 3550 strictly mandates these must start at random values so the browser
  // doesn't confuse a new session with delayed packets from an old session
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<uint16_t> seq_dist(0, 0xFFFF);
  std::uniform_int_distribution<uint32_t> ts_dist(0, 0xFFFFFFFF);

  sequence_num_ = seq_dist(gen);
  timestamp_ = ts_dist(gen);
}

size_t RTPPacketizer::packetize(boost::span<uint8_t> payload,
                                boost::span<uint8_t> out_buffer) {
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

void RTPPacketizer::update_timestamp() { timestamp_ += timestamp_increment_; }

uint32_t RTPPacketizer::current_timestamp() const { return timestamp_; }
}  // namespace hermes::net::rtp
