// RTPTransmitter.cpp
#include "boost/core/span.hpp"
#include <cstdint>
#include <iostream>
#include <RTPTransmitter.hpp>

using boost::asio::ip::udp;
RTPTransmitter::RTPTransmitter(boost::asio::io_context &io,
                               const std::string &remoteAddr,
                               uint16_t remotePort)
    : socket_(io){
  socket_.open(boost::asio::ip::udp::v4());
  udp::resolver resolver(io);

        // 3. Resolve the hostname and port
        // This looks up "host.docker.internal" and gives us an endpoint iterator
        udp::resolver::results_type endpoints =
            resolver.resolve(udp::v4(), remoteAddr, std::to_string(remotePort));

        // 4. Store the first valid endpoint found
        if (endpoints != udp::resolver::results_type()) {
            remoteEndpoint_ = *endpoints.begin();
        } else {
            throw std::runtime_error("Could not resolve host: " + remoteAddr);
        }
}

void RTPTransmitter::stop() {
  std::cout << "RTPTransmitter stopping...\n";

  if (socket_.is_open()) {
    boost::system::error_code ec;
    socket_.cancel(ec);
    if (ec) {
      std::cout << "Socket cancel error: " << ec.message() << "\n";
    }

    // Don't close socket in stop() - let destructor handle it
    // socket_.close(ec);
  }
}

void RTPTransmitter::asyncSend(boost::span<const uint8_t> buf,
                               std::size_t size) {

  socket_.async_send_to(
      boost::asio::buffer(buf,
                          size), // Only send 'size' bytes, not the whole buffer
      remoteEndpoint_,
      [buf](const boost::system::error_code &ec, std::size_t bytesSent) {
        if (ec) {
          std::cerr << "Send error: " << ec.message() << "\n";
        }
      });
}
