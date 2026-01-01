#pragma once
#include <cstdint>

#include "boost/core/span.hpp"

/**
 * @brief Encapsulates raw audio payload into RFC 3550 RTP packets.
 * * Handles the maintenance of Sequence Numbers and Timestamps, which are critical
 * for the client (receiver) to play audio back at the correct speed and order.
 */
class RTPPacketizer {
   public:
    /**
     * @param timestampIncrement How many timestamp units to advance per packet.
     * For 20ms of 8kHz audio, this is typically 160 units.
     */
    RTPPacketizer(uint8_t payload_type, uint32_t ssrc, uint32_t timestampIncrement);

    /**
     * @brief Serializes payload + header into the output buffer.
     * @return The total size of the packet (Header + Payload) in bytes.
     * @warning 'outBuffer' must be at least 12 bytes larger than 'payload'.
     */
    size_t packetize(boost::span<uint8_t> payload, boost::span<uint8_t> outBuffer);

    void updateTimestamp();
  
    uint32_t currentTimestamp() const;

   private:
    uint8_t payloadType_;
    uint32_t ssrc_;
    uint16_t sequenceNum_ = 0;
    uint32_t timestamp_ = 0;
    uint32_t timestampIncrement_;
    static constexpr uint8_t version_ = 2;
};
