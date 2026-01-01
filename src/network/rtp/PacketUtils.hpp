#pragma once
#include <iostream>
#include <span>

#include "CodecStrategy.hpp"
#include "RTPPacketizer.hpp"
#include "spdlog/spdlog.h"

/**
 * @brief Assembles RTP packets in-place (zero-copy) to avoid allocation.
 */
namespace PacketUtils {

static constexpr size_t RTP_HEADER_SIZE = 12;

inline size_t packet2rtp(std::span<const uint8_t> pcmFrame, RTPPacketizer& packetizer,
                         ICodecStrategy& codec, std::span<uint8_t> outBuffer) {
    if (outBuffer.size() < RTP_HEADER_SIZE) {
        std::cerr << "[PacketUtils] Buffer too small for header\n";
        return 0;
    }

    auto payload_buffer = outBuffer.subspan(RTP_HEADER_SIZE);

    size_t encoded_size = codec.Encode(pcmFrame, payload_buffer);

    if (encoded_size == 0) {
        std::cerr << "[PacketUtils] Encoding failed or buffer too small\n";
        return 0;
    }

    auto actual_payload = payload_buffer.first(encoded_size);

    return packetizer.packetize(actual_payload, outBuffer);
}
}
