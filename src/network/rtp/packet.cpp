#include <packet.hpp>
#include <boost/asio.hpp>
#include <cstdint>  
#include <optional>
#include <random>
#include <vector>
#ifdef _WIN32
#include <WinSock2.h>
#else
#include <arpa/inet.h>

#endif

[[nodiscard]] uint32_t generate_ssrc() {
  static std::random_device dev;
  static std::mt19937_64 rng(dev());
  static std::uniform_int_distribution<uint32_t> dist(1, 100);
  const auto ssrc = dist(rng);

  return ssrc;
}

static constexpr size_t MINIMUM_HEADER_SIZE = 12;
static constexpr size_t MINIMUM_EXTENSION_SIZE = 4;

RTPPacket::RTPPacket(Header header, boost::span<const uint8_t> payload)
    : header(std::move(header)), payload(payload) {}

RTPPacket::RTPPacket(RTPPacket&& other) noexcept
    : header(std::move(other.header)), payload(other.payload) {}

RTPPacket& RTPPacket::operator=(RTPPacket&& other) noexcept {
    header = std::move(other.header);
    payload = other.payload;
    return *this;
}


RTPPacket::Header::Header(const bool padding, const uint8_t version,
                          const uint8_t payload_type, const bool marker,
                          const uint16_t sequence_num, const uint32_t timestamp,
                          const uint32_t ssrc, std::vector<uint32_t> csrc_list,
                          std::optional<Extension> extension)
    : padding(padding), version(version), payload_type(payload_type),
      marker(marker), sequence_num(sequence_num), timestamp(timestamp),
      ssrc(ssrc), csrc_list(std::move(csrc_list)),
      extension(std::move(extension)) {}

RTPPacket::Header::Header(RTPPacket::Header &&other) noexcept
    : padding(other.padding), version(other.version),
      payload_type(other.payload_type), marker(other.marker),
      sequence_num(other.sequence_num), timestamp(other.timestamp),
      ssrc(other.ssrc), csrc_list(std::move(other.csrc_list)),
      extension(std::move(other.extension)) {}

RTPPacket::Header &
RTPPacket::Header::operator=(RTPPacket::Header &&other) noexcept {
  padding = other.padding;
  version = other.version;
  payload_type = other.payload_type;
  marker = other.marker;
  sequence_num = other.sequence_num;
  timestamp = other.timestamp;
  ssrc = other.ssrc;
  csrc_list = std::move(other.csrc_list);
  extension = std::move(other.extension);

  return *this;
}

RTPPacket::Header::Extension::Extension(const uint16_t id,
                                        std::vector<uint32_t> data)
    : id(id), data(std::move(data)) {}

RTPPacket::Header::Extension::Extension(
    RTPPacket::Header::Extension &&other) noexcept
    : id(other.id), data(std::move(other.data)) {}

RTPPacket::Header::Extension &RTPPacket::Header::Extension::operator=(
    RTPPacket::Header::Extension &&other) noexcept {
  id = other.id;
  data = std::move(other.data);

  return *this;
}

void RTPPacket::add_ssrc(const uint32_t new_ssrc) {
  header.csrc_list.push_back(header.ssrc);
  header.ssrc = new_ssrc;
}

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
// For functions from_buffer/to_buffer.
// We need to analyze the rtp buffer, which is, by nature,
// an unsafe operation (And requires pointer arithmetic).

// We dont want alignment linting errors because the alignment of these
// header structs need to be precise as they are cast directly from RTP buffer.
// NOLINTBEGIN(altera-struct-pack-align)

#pragma pack(push, 1)
struct FirstOctet {
  uint8_t cc : 4;
  uint8_t extension : 1;
  uint8_t padding : 1;
  uint8_t version : 2;
};

struct SecondOctet {
  uint8_t payload_type : 7;
  uint8_t marker : 1;
};
#pragma pack(pop)


std::optional<boost::span<uint8_t>>
RTPPacket::to_buffer(const boost::span<uint8_t>& packet_buffer) const {
    const size_t csrc_list_size = header.csrc_list.size() * sizeof(uint32_t);
    const size_t extension_size = header.extension
        ? MINIMUM_EXTENSION_SIZE + header.extension->data.size() * sizeof(uint32_t)
        : 0;

    size_t buffer_size = MINIMUM_HEADER_SIZE + csrc_list_size +
                         extension_size + payload.size();

    if (buffer_size > packet_buffer.size()) {
        return {};
    }

    uint8_t* buf_ptr = packet_buffer.data();

    const FirstOctet first_octet = {
        static_cast<uint8_t>(header.csrc_list.size()),
        header.extension.has_value(),
        header.padding,
        header.version
    };
    *reinterpret_cast<FirstOctet*>(buf_ptr) = first_octet;
    buf_ptr += sizeof(first_octet);

    const SecondOctet second_octet = {header.payload_type, header.marker};
    *reinterpret_cast<SecondOctet*>(buf_ptr) = second_octet;
    buf_ptr += sizeof(second_octet);

    *reinterpret_cast<uint16_t*>(buf_ptr) = htons(header.sequence_num);
    buf_ptr += sizeof(header.sequence_num);

    *reinterpret_cast<uint32_t*>(buf_ptr) = htonl(header.timestamp);
    buf_ptr += sizeof(header.timestamp);

    *reinterpret_cast<uint32_t*>(buf_ptr) = htonl(header.ssrc);
    buf_ptr += sizeof(header.ssrc);

    for (uint32_t csrc : header.csrc_list) {
        *reinterpret_cast<uint32_t*>(buf_ptr) = htonl(csrc);
        buf_ptr += sizeof(uint32_t);
    }

    if (header.extension) {
        *reinterpret_cast<uint16_t*>(buf_ptr) = htons(header.extension->id);
        buf_ptr += sizeof(header.extension->id);

        uint16_t extension_data_size =
            static_cast<uint16_t>(header.extension->data.size());
        *reinterpret_cast<uint16_t*>(buf_ptr) = htons(extension_data_size);
        buf_ptr += sizeof(extension_data_size);

        for (uint32_t val : header.extension->data) {
            *reinterpret_cast<uint32_t*>(buf_ptr) = htonl(val);
            buf_ptr += sizeof(uint32_t);
        }
    }

    // Now just memcpy the payload span
    std::memcpy(buf_ptr, payload.data(), payload.size());

    return boost::span<uint8_t>(packet_buffer.data(), buffer_size);
}

// NOLINTEND(altera-struct-pack-align)

std::optional<RTPPacket>
RTPPacket::from_buffer(const boost::span<uint8_t>& buffer) {
    if (buffer.size() < MINIMUM_HEADER_SIZE) {
        return {};
    }

    const uint8_t* buf_ptr = buffer.data();
    const auto* first_octet = reinterpret_cast<const FirstOctet*>(buf_ptr);

    size_t expected_size = MINIMUM_HEADER_SIZE;
    expected_size += static_cast<size_t>(first_octet->cc) * sizeof(uint32_t);
    if (buffer.size() < expected_size) {
        return {};
    }

    buf_ptr += sizeof(*first_octet);

    const auto* second_octet = reinterpret_cast<const SecondOctet*>(buf_ptr);
    buf_ptr += sizeof(*second_octet);

    uint16_t sequence_num = ntohs(*reinterpret_cast<const uint16_t*>(buf_ptr));
    buf_ptr += sizeof(sequence_num);

    uint32_t timestamp = ntohl(*reinterpret_cast<const uint32_t*>(buf_ptr));
    buf_ptr += sizeof(timestamp);

    uint32_t ssrc = ntohl(*reinterpret_cast<const uint32_t*>(buf_ptr));
    buf_ptr += sizeof(ssrc);

    std::vector<uint32_t> csrc_list(first_octet->cc);
    for (uint8_t i = 0; i < first_octet->cc; i++) {
        csrc_list[i] = ntohl(*reinterpret_cast<const uint32_t*>(buf_ptr));
        buf_ptr += sizeof(uint32_t);
    }

    std::optional<Header::Extension> extension;
    if (first_octet->extension) {
        expected_size += MINIMUM_EXTENSION_SIZE;
        if (buffer.size() < expected_size) {
            return {};
        }

        uint16_t id = ntohs(*reinterpret_cast<const uint16_t*>(buf_ptr));
        buf_ptr += sizeof(id);

        uint16_t extension_data_size =
            ntohs(*reinterpret_cast<const uint16_t*>(buf_ptr));
        buf_ptr += sizeof(extension_data_size);

        expected_size += extension_data_size * sizeof(uint32_t);
        if (buffer.size() < expected_size) {
            return {};
        }

        std::vector<uint32_t> extension_data(extension_data_size);
        for (uint16_t i = 0; i < extension_data_size; i++) {
            extension_data[i] = ntohl(*reinterpret_cast<const uint32_t*>(buf_ptr));
            buf_ptr += sizeof(uint32_t);
        }
        extension = Header::Extension(id, std::move(extension_data));
    }

    Header hdr(first_octet->padding, first_octet->version,
               second_octet->payload_type, second_octet->marker, sequence_num,
               timestamp, ssrc, std::move(csrc_list), std::move(extension));

    //Instead of allocating vector<uint8_t>, create span
    size_t header_bytes = buf_ptr - buffer.data();
    boost::span<const uint8_t> payload_span(buffer.data() + header_bytes,
                                            buffer.size() - header_bytes);

    return RTPPacket(std::move(hdr), payload_span);
}


// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
