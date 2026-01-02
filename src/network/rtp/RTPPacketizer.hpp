#pragma once
#include <cstdint>
#include <boost/core/span.hpp>

/**
 * @brief RFC 3550 RTP packetizer. Manages sequence numbers and timestamps.
 */
class RTPPacketizer {
   public:
    /**
     * @param timestamp_increment How many timestamp units to advance per packet.
     */
    RTPPacketizer(uint8_t payload_type, uint32_t ssrc, uint32_t timestamp_increment);

    /**
     * @brief Serializes payload + header into the output buffer.
     * @return The total size of the packet (Header + Payload) in bytes.
     * @warning 'out_buffer' must be at least 12 bytes larger than 'payload'.
     */
    size_t packetize(boost::span<uint8_t> payload, boost::span<uint8_t> out_buffer);

    /** @brief Advances the internal timestamp by the increment value. */
    void update_timestamp();

    /** @brief Returns the current RTP timestamp. */
    uint32_t current_timestamp() const;

   private:
    uint8_t payload_type_;
    uint32_t ssrc_;
    uint16_t sequence_num_ = 0;
    uint32_t timestamp_ = 0;
    uint32_t timestamp_increment_;

    static constexpr uint8_t version_ = 2;
};
