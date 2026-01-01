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

    /**
     * @brief Initiates the audio graph execution.
     * * 1. Fetches required files (S3).
     * 2. Pre-fills audio buffers.
     * 3. Starts the 20ms ticker loop.
     * * @return A coroutine that runs until the session ends or is stopped.
     */
    boost::asio::awaitable<void> start();
    void stop();

    void AddClient(std::string& ip, uint16_t port);
    void AttachObserver(std::shared_ptr<ISessionObserver> observer);

   private:
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};
