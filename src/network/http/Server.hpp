#pragma once
#include <memory>
#include <string>
#include <boost/asio/io_context.hpp> // Minimal include for reference

class Server : public std::enable_shared_from_this<Server> {
public:
    Server(boost::asio::io_context& io, const std::string& address, const std::string& port, unsigned int num_threads);
    ~Server(); // Destructor must be defined in .cpp

    void Start();
    void Stop();

private:
    struct Impl; // Forward declaration
    std::unique_ptr<Impl> pImpl_;
};
