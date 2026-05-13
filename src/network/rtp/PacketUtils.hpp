#pragma once
#include <span>

#include "CodecStrategy.hpp"
#include "EncryptionStrategy.hpp"
#include "RTPPacketizer.hpp"
#include "spdlog/spdlog.h"
/**
 * @brief Assembles RTP packets in-place (zero-copy) to avoid allocation.
 */
namespace hermes::net::rtp {

static constexpr size_t RTP_HEADER_SIZE = 12;

inline size_t packet_to_rtp(std::span<const uint8_t> pcmFrame,
                            RTPPacketizer& packetizer,
                            audio::ICodecStrategy& codec,
                            crypto::IEncryptionStrategy* encryptor,
                            std::span<uint8_t> outBuffer) {
  if (outBuffer.size() < RTP_HEADER_SIZE) {
    spdlog::error("[PacketUtils] Buffer too small for header");
    return 0;
  }

  auto payload_buffer = outBuffer.subspan(RTP_HEADER_SIZE);

  size_t encoded_size = codec.encode(pcmFrame, payload_buffer);

  if (encoded_size == 0) {
    spdlog::error("[PacketUtils] Encoding failed or buffer too small");
    return 0;
  }

  auto actual_payload = payload_buffer.first(encoded_size);
  return packetizer.packetize(actual_payload, outBuffer, encryptor);
}
}  // namespace hermes::net::rtp
