#include "RTPPacketizer.hpp"

#include <Packet.hpp>
#include <cstdint>
#include <ctime>
#include <random>

#include "EncryptionStrategy.hpp"
#include "boost/core/span.hpp"
namespace hermes::net::rtp {
RTPPacketizer::RTPPacketizer(uint8_t payload_type, uint32_t ssrc,  // NOLINT
                             uint32_t timestamp_increment)  // NOLINT
    : payload_type_(payload_type),
      ssrc_(ssrc),
      timestamp_increment_(timestamp_increment) {
  // since we also support webrtc
  //  RFC 3550 strictly mandates these must start at random values so the
  //  browser doesn't confuse a new session with delayed packets from an old
  //  session
  std::random_device rd;
  std::mt19937 gen(rd());
  constexpr uint16_t MAX_SEQ = 0xFFFF;
  constexpr uint32_t MAX_TS = 0xFFFFFFFF;
  std::uniform_int_distribution<uint16_t> seq_dist(0, MAX_SEQ);
  std::uniform_int_distribution<uint32_t> ts_dist(0, MAX_TS);

  sequence_num_ = seq_dist(gen);
  timestamp_ = ts_dist(gen);
}

/**
 * @brief Packetizes audio payload into an RTP packet, handling sequence numbering and timestamping.
 *
 * It uses a 48-bit SRTP index formed by a 32-bit Roll-Over Counter (ROC) and a 16-bit sequence number.
 * The ROC prevents the sequence number from wrapping around, ensuring a unique index for encryption.
 */
size_t RTPPacketizer::packetize(boost::span<uint8_t> payload,  // NOLINT
                                boost::span<uint8_t> out_buffer,  // NOLINT
                                crypto::IEncryptionStrategy* ency) {
  uint16_t current_seq = sequence_num_;


  sequence_num_++;


  if (sequence_num_ == 0) {
    roc_++;
  }

  RTPPacket::Header hdr{/* padding */ false,
                        version_,
                        /* payload_type */ payload_type_,
                        /* marker */ false,
                        current_seq,
                        timestamp_,
                        ssrc_,
                        /* csrc_list */ {},
                        /* extension */ std::nullopt};

  if (ency != nullptr) {
    constexpr int ROC_SHIFT = 16;
    uint64_t packet_index = (static_cast<uint64_t>(roc_) << ROC_SHIFT) | current_seq;
    ency->encrypt(payload, packet_index);
  }

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

uint32_t RTPPacketizer::current_ssrc() const { return ssrc_; }
}  // namespace hermes::net::rtp
