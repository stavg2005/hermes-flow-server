#pragma once

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <memory>
#include <string>

#include "ISessionObserver.hpp"


// Forward declarations
class Graph;

class Session : public std::enable_shared_from_this<Session> {
   public:
    Session(boost::asio::io_context& io, std::string id, Graph&& g);
    ~Session();  // Destructor requires Impl to be complete in .cpp

    boost::asio::awaitable<void> start();
    boost::asio::awaitable<void> stop();

    void AddClient(std::string& ip, uint16_t port);
    void AttachObserver(std::shared_ptr<ISessionObserver> observer);

   private:
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};
