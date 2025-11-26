#pragma once
#include <boost/core/span.hpp>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include "config.hpp"
#include "RTPPacketizer.hpp"
#include "alaw.hpp"          // your new span-based encoder

namespace PacketUtils {

    static constexpr size_t HEADER_SIZE =12;
/* ------------------------------------------------------------------
   Convert raw PCM-16 bytes → A-law → RTP
   ------------------------------------------------------------------ */

inline int frameToRtp(boost::span<const uint8_t> pcmFrame,
                      RTPPacketizer&             packetizer,
                      boost::span<uint8_t>       outBuffer)
{/* 1. Calculate Sample Count */
    // 320 bytes PCM / 2 = 160 samples
    const size_t sample_count = pcmFrame.size() / sizeof(int16_t);

    /* 2. Safety Check: Does the Output Buffer fit Header + Payload? */
    // We need 12 bytes for header + 1 byte per sample (A-Law)
    if (outBuffer.size() < RTP_HEADER_SIZE + sample_count) {
        std::cerr << "[PacketUtils] Error: Output buffer too small. "
                  << "Need " << (RTP_HEADER_SIZE + sample_count)
                  << ", got " << outBuffer.size() << "\n";
        return 0;
    }

    /* 3. Reinterpret input as 16-bit samples */
    auto samples = boost::span<const int16_t>(
        reinterpret_cast<const int16_t*>(pcmFrame.data()),
        sample_count);

    /* 4. Define where the Payload goes (Directly after the header) */
    // This supports the "Zero Copy" strategy by writing A-Law directly
    // into the final memory location.
    boost::span<uint8_t> payload(outBuffer.data() + HEADER_SIZE, sample_count);

    /* 5. Encode A-law directly into that area */
    encode_alaw(samples, payload);

    /* 6. Finalize the Packet */
    // The packetizer will write the header to outBuffer[0...11]
    return packetizer.packetize(payload, outBuffer);
}



inline std::size_t packet2rtp(boost::span<const uint8_t> pcmFrame,
                              RTPPacketizer&             packetizer,
                              boost::span<uint8_t>       rtpBuffer)
{
    int bytes = frameToRtp(pcmFrame, packetizer, rtpBuffer);

    if (bytes == 0) {
        std::cerr << "[PacketUtils] packetizer returned 0 bytes\n";
        return 0;
    }
    return static_cast<std::size_t>(bytes);
}

}
