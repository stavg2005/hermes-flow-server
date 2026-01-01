#pragma once

#include <boost/asio/io_context.hpp>
#include <memory>
#include <string>


/**
 * @brief High-level HTTP Server Facade.
 *    Uses PIMPL for ABI stability
 */
class Server : public std::enable_shared_from_this<Server> {
   public:
    Server(boost::asio::io_context& io, const std::string& address, const std::string& port,
           unsigned int num_threads);
    ~Server();

    void Start();
    void Stop();

   private:
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};
