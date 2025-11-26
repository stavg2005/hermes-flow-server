#pragma once
#include <boost/core/span.hpp>
#include <cstddef>
#include <cstdint>
#include <iostream>

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
{
    /* reinterpret the incoming PCM bytes as 16-bit samples */
    auto samples = boost::span<const int16_t>(
        reinterpret_cast<const int16_t*>(pcmFrame.data()),
        pcmFrame.size() / sizeof(int16_t));

    /* Reserve the RTP payload area directly inside outBuffer*/
    boost::span<uint8_t> payload(outBuffer.data() + HEADER_SIZE, samples.size());

    /* Encode A-law directly into that payload area */
    encode_alaw(samples, payload);

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
