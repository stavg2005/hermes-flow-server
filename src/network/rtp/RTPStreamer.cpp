#include "RTPStreamer.hpp"

// Crypto++ is completely isolated to this translation unit
#include <cryptopp/hkdf.h>
#include <cryptopp/sha.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <random>

#include "Config.hpp"
#include "Packet.hpp"
#include "PacketUtils.hpp"

namespace hermes::net::rtp {

 std::unique_ptr<RTPStreamer> RTPStreamer::create(
    boost::asio::io_context& io, const config::CryptoConfig& crypto_cfg) {
  return std::make_unique<RTPStreamer>(io, crypto_cfg);
}

RTPStreamer::RTPStreamer(boost::asio::io_context& io,
                         const hermes::config::CryptoConfig& crypto_cfg)
    : socket_(io,
              boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 0)),
      session_master_key_(crypto_cfg.master_key),
      session_salt_(crypto_cfg.salt) {
  codec_ = std::make_unique<audio::ALawCodecStrategy>();
  packetizer_ = std::make_unique<RTPPacketizer>(
      codec_->get_payload_type(), generate_ssrc(),
      codec_->get_timestamp_increment(config::FRAME_SIZE_BYTES));

  for (int i = 0; i < RING_BUFFER_SIZE; ++i) {
    packet_ring_[i] = std::make_shared<std::vector<uint8_t>>(
        RTP_HEADER_SIZE + config::FRAME_SIZE_BYTES);
  }
}

void RTPStreamer::add_client(const std::string& host_or_ip, uint16_t port,
                             double packet_loss_ratio) {
  boost::system::error_code ec;
  auto addr = boost::asio::ip::make_address(host_or_ip, ec);
  udp::endpoint ep;

  if (!ec) {
    ep = udp::endpoint(addr, port);
  } else {
    asio::ip::udp::resolver resolver(socket_.get_executor());
    auto results = resolver.resolve(asio::ip::udp::v4(), host_or_ip,
                                    std::to_string(port), ec);
    if (ec || results.empty()) {
      spdlog::error("Invalid IP or Hostname: {} - {}", host_or_ip,
                    ec.message());
      return;
    }
    ep = *results.begin();
  }

  if (std::find_if(clients_.begin(), clients_.end(),
                   [&ep](const RtpClientTarget& client) {
                     return client.endpoint == ep;
                   }) == clients_.end()) {
    clients_.push_back({ep, packet_loss_ratio});
    spdlog::info("RTP Client added: {}:{} (Loss: {}%)",
                 ep.address().to_string(), port, packet_loss_ratio * 100.0);
  }
}

void RTPStreamer::remove_client(const std::string& host_or_ip, uint16_t port) {
  try {
    boost::system::error_code ec;
    auto addr = asio::ip::make_address(host_or_ip, ec);
    udp::endpoint ep;

    if (!ec) {
      ep = udp::endpoint(addr, port);
    } else {
      asio::ip::udp::resolver resolver(socket_.get_executor());
      auto results = resolver.resolve(asio::ip::udp::v4(), host_or_ip,
                                      std::to_string(port), ec);
      if (ec || results.empty()) {
        return;
      }
      ep = *results.begin();
    }

    std::erase(clients_, ep);
    spdlog::info("RTP Client removed: {}:{}", ep.address().to_string(), port);
  } catch (const std::exception& e) {
    spdlog::trace("Ignored exception during client removal: {}", e.what());
  }
}

std::vector<uint8_t> RTPStreamer::derive_iv_from_ssrc(uint32_t ssrc) {
  std::vector<uint8_t> iv = session_salt_;

  constexpr int SHIFT_24 = 24;
  constexpr int SHIFT_16 = 16;
  constexpr int SHIFT_8 = 8;
  constexpr uint32_t BYTE_MASK = 0xFF;

  iv[0] ^= (ssrc >> SHIFT_24) & BYTE_MASK;
  iv[1] ^= (ssrc >> SHIFT_16) & BYTE_MASK;
  iv[2] ^= (ssrc >> SHIFT_8) & BYTE_MASK;
  iv[3] ^= ssrc & BYTE_MASK;

  return iv;
}

std::vector<uint8_t> RTPStreamer::derive_session_key(uint32_t ssrc) {
  constexpr size_t AES_KEY_SIZE = 16;
  std::vector<uint8_t> session_key(AES_KEY_SIZE);

  constexpr int SHIFT_24 = 24;
  constexpr int SHIFT_16 = 16;
  constexpr int SHIFT_8 = 8;
  constexpr uint32_t BYTE_MASK = 0xFF;

  uint8_t ssrc_bytes[4] = {static_cast<uint8_t>((ssrc >> SHIFT_24) & BYTE_MASK),
                           static_cast<uint8_t>((ssrc >> SHIFT_16) & BYTE_MASK),
                           static_cast<uint8_t>((ssrc >> SHIFT_8) & BYTE_MASK),
                           static_cast<uint8_t>(ssrc & BYTE_MASK)};

  CryptoPP::HKDF<CryptoPP::SHA256> hkdf;
  hkdf.DeriveKey(session_key.data(), session_key.size(),
                 session_master_key_.data(),  // Use the injected master key
                 session_master_key_.size(), ssrc_bytes, 4, nullptr, 0);

  return session_key;
}

void RTPStreamer::setup_security() {
  const auto& ssrc = packetizer_->current_ssrc();
  spdlog::debug("SSRC is {}", ssrc);
  encryptor_ = crypto::create_aes_encryption_strategy(
      derive_session_key(ssrc), derive_iv_from_ssrc(ssrc));
  encryption_enabled_ = true;
  spdlog::info("RTP Security Layer initialized successfully");
}

void RTPStreamer::send_frame(std::span<const uint8_t> pcm_frame) {
  if (clients_.empty()) {
    return;
  }

  auto packet_owner = packet_ring_[ring_index_];
  ring_index_ = (ring_index_ + 1) % RING_BUFFER_SIZE;
  std::span<uint8_t> packet_span(*packet_owner);

  size_t packet_size = packet_to_rtp(pcm_frame, *packetizer_, *codec_,
                                     encryptor_.get(), packet_span);
  if (packet_size == 0) {
    return;
  }

  static thread_local std::mt19937 gen(std::random_device{}());
  std::uniform_real_distribution<double> dist(0.0, 1.0);

  for (const auto& client : clients_) {
    if (client.packet_loss_ratio > 0.0) {
      if (dist(gen) < client.packet_loss_ratio) {
        continue;
      }
    }

    socket_.async_send_to(
        asio::buffer(*packet_owner, packet_size), client.endpoint,
        [this, packet_owner](const boost::system::error_code& ec,
                             std::size_t bytes_transferred) {
          if (!ec) {
            bytes_sent_.fetch_add(bytes_transferred, std::memory_order_relaxed);
            packets_sent_.fetch_add(1, std::memory_order_relaxed);
          } else {
            spdlog::debug("UDP send failed: {}", ec.message());
          }
        });
  }
}

}  // namespace hermes::net::rtp
