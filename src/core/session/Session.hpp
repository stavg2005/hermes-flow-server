#pragma once

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <memory>
#include <string>

#include "AudioExecutor.hpp"
#include "ISessionObserver.hpp"
#include "RTPStreamer.hpp"
#include "Types.hpp"

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

    void AddClient(const std::string& ip, uint16_t port);
    void AttachObserver(std::shared_ptr<ISessionObserver> observer);
    bool get_is_running() const;
    void ConfigureStreamerFromGraph();

   private:
    net::io_context& io_;
    std::string id_;
    std::atomic<bool> is_running_{false};
    std::shared_ptr<Graph> graph_;
    std::unique_ptr<AudioExecutor> audio_executor_;
    std::unique_ptr<RTPStreamer> streamer_;

    net::steady_timer timer_;
    std::shared_ptr<ISessionObserver> observer_;
};
