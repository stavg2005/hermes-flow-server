#pragma once

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <memory>
#include <string>

#include "ISessionObserver.hpp"


class Graph;

class Session : public std::enable_shared_from_this<Session> {
   public:
    Session(boost::asio::io_context& io, std::string id, Graph&& g);
    ~Session();  // Destructor requires Impl to be complete in .cpp

    /**
     * @brief starts the the audio graph execution.Fetches required files (S3),
     *  Pre-fills audio buffers,
     *  Starts the 20ms ticker loop
     */
    boost::asio::awaitable<void> start();
    void stop();

    void AddClient(std::string& ip, uint16_t port);
    void AttachObserver(std::shared_ptr<ISessionObserver> observer);
    bool get_is_running();
   private:
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};
