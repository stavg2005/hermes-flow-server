#pragma once
#include <memory>
#include <string>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_awaitable.hpp>
namespace net = boost::asio;
// Forward declarations - No need to include the actual headers!
class AudioExecutor;
class RTPStreamer;
class Graph;

class Session : public std::enable_shared_from_this<Session> {
public:
    Session(boost::asio::io_context& io, std::string id, Graph&& g);
    ~Session(); // Destructor must be defined in .cpp where Impl is complete

    net::awaitable<void>  start();
    net::awaitable<void>  stop();

private:
    struct Impl; // Opaque pointer
    std::unique_ptr<Impl> pImpl_;
};
