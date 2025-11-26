#pragma once
#include <iostream>
#include <span>
#include "CodecStrategy.hpp"
#include "RTPPacketizer.hpp"
#include "spdlog/spdlog.h"
namespace PacketUtils {

static constexpr size_t RTP_HEADER_SIZE = 12;  //

// Generic function handling ANY codec strategy
inline size_t packet2rtp(std::span<const uint8_t> pcmFrame, RTPPacketizer& packetizer,
                         ICodecStrategy& codec,  // <--- Dependency Injection
                         std::span<uint8_t> outBuffer) {
    // 1. Validate Header Space
    if (outBuffer.size() < RTP_HEADER_SIZE) {
        std::cerr << "[PacketUtils] Buffer too small for header\n";
        return 0;
    }

    // 2. Define Payload Location (Offset by 12 bytes)
    auto payload_buffer = outBuffer.subspan(RTP_HEADER_SIZE);

    // 3. Execute Strategy: Encode directly into the target buffer
    size_t encoded_size = codec.Encode(pcmFrame, payload_buffer);

    if (encoded_size == 0) {
        std::cerr << "[PacketUtils] Encoding failed or buffer too small\n";
        return 0;
    }

    // 4. Create the correct view of the payload
    auto actual_payload = payload_buffer.first(encoded_size);

    // 5. Let Packetizer write the header
    // Note: Packetizer will treat 'actual_payload' as data to be "packetized".
    // Ensure RTPPacketizer::packetize correctly handles payload overlapping
    // with outBuffer if you want true zero-copy, or accepts it as is.
    return packetizer.packetize(actual_payload, outBuffer);
}
}  // namespace PacketUtils
