#pragma once

#include <memory>
#include <string>
#include <boost/asio/io_context.hpp>

/**
 * @brief High-level Server Facade.
 * Orchestrates the thread pool, listener, and routing components.
 */
class Server : public std::enable_shared_from_this<Server> {
public:
    Server(boost::asio::io_context& io, const std::string& address, const std::string& port, unsigned int num_threads);
    ~Server();

    void Start();
    void Stop();

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};
