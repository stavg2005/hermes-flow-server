#include "RTPTransmitter.hpp"

#include <iostream>

#include "spdlog/spdlog.h"

using boost::asio::ip::udp;

RTPTransmitter::RTPTransmitter(boost::asio::io_context& io,
                               const std::string& remoteAddr,
                               uint16_t remotePort)
    : socket_(io) {
  udp::resolver resolver(io);
  udp::resolver::results_type endpoints =
      resolver.resolve(udp::v4(), remoteAddr, std::to_string(remotePort));

  if (endpoints != udp::resolver::results_type()) {
    remoteEndpoint_ = *endpoints.begin();
    spdlog::debug("RTP Transmitter resolved: {}:{}",
                  remoteEndpoint_.address().to_string(),
                  remoteEndpoint_.port());
  } else {
    throw std::runtime_error("RTP Host resolution failed: " + remoteAddr);
  }

  socket_.open(udp::v4());
}

void RTPTransmitter::stop() {
  if (socket_.is_open()) {
    boost::system::error_code ec;
    socket_.cancel(ec);
    if (ec) {
      spdlog::warn("RTP Socket cancel error: {}", ec.message());
    }
    // Destructor closes the socket automatically
  }
}

void RTPTransmitter::asyncSend(std::shared_ptr<std::vector<uint8_t>> data,
                               std::size_t size) {
  socket_.async_send_to(
      boost::asio::buffer(*data, size), remoteEndpoint_,
      [data](const boost::system::error_code& ec, std::size_t /*bytes*/) {
        // 'data' is kept alive by the lambda capture
        if (ec) {
          spdlog::error("RTP Send Error: {}", ec.message());
        }
      });
}
