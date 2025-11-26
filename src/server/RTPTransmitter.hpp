#pragma once
#include <boost/asio.hpp>
#include <boost/core/span.hpp>
#include <cstdint>

class RTPTransmitter {
public:



  RTPTransmitter(boost::asio::io_context &io, const std::string &remoteAddr,
                 uint16_t remotePort);
  void asyncSend(std::shared_ptr<std::vector<uint8_t>> data, std::size_t size);
  void stop();
private:
  boost::asio::ip::udp::socket socket_;
  boost::asio::ip::udp::endpoint remoteEndpoint_;
};
