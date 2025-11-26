#pragma once
#include <boost/core/span.hpp>
#include <boost/asio.hpp>
#include <cstdint>
#include <optional>
#include <vector>

[[nodiscard]] uint32_t generate_ssrc();

struct RTPPacket {
    struct Header {
        bool padding;
        uint8_t version;
        uint8_t payload_type;
        bool marker;
        uint16_t sequence_num = 0;
        uint32_t timestamp = 0;
        uint32_t ssrc = 0;

        std::vector<uint32_t> csrc_list;

        struct Extension {
            uint16_t id = 0;
            std::vector<uint32_t> data;

            Extension() = default;
            Extension(uint16_t id, std::vector<uint32_t> data);
            Extension(Extension&& other) noexcept;
            Extension(const Extension&) = default;
            Extension& operator=(Extension&& other) noexcept;
            Extension& operator=(const Extension&) = default;
        };

        std::optional<Extension> extension;

        Header() = default;
        Header(bool padding, uint8_t version, uint8_t payload_type, bool marker,
               uint16_t sequence_num, uint32_t timestamp, uint32_t ssrc,
               std::vector<uint32_t> csrc_list,
               std::optional<Extension> extension);
        Header(Header&& other) noexcept;
        Header(const Header&) = default;
        Header& operator=(Header&& other) noexcept;
        Header& operator=(const Header&) = default;
    } header;

    boost::span<const uint8_t> payload;

    RTPPacket() = default;
    RTPPacket(Header header, boost::span<const uint8_t> payload);
    RTPPacket(RTPPacket&& other) noexcept;
    RTPPacket(const RTPPacket&) = default;
    RTPPacket& operator=(RTPPacket&& other) noexcept;
    RTPPacket& operator=(const RTPPacket&) = default;

    [[nodiscard]] static std::optional<RTPPacket>
    from_buffer(const boost::span<uint8_t>& buffer);

    void add_ssrc(uint32_t new_ssrc);

    // ✅ Caller provides packet_buffer to fill, payload is copied into it
    std::optional<boost::span<uint8_t>>
    to_buffer(const boost::span<uint8_t>& packet_buffer) const;
};
