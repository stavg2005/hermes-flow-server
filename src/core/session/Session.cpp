#include "Session.hpp"

#include "AudioExecutor.hpp"
#include "RTPStreamer.hpp"
#include "Nodes.hpp"
#include <boost/asio.hpp>

namespace net = boost::asio;

struct Session::Impl {
    boost::asio::io_context& io_;
    std::string id_;
    std::unique_ptr<AudioExecutor> executor_;
    std::unique_ptr<RTPStreamer> streamer_;
    net::steady_timer timer_;

    Impl(boost::asio::io_context& io, std::string id, Graph&& g)
        : io_(io), id_(std::move(id)), timer_(io) {
        executor_ = std::make_unique<AudioExecutor>(io_, std::make_unique<Graph>(std::move(g)));

        streamer_ = std::make_unique<RTPStreamer>(io_, "127.0.0.1", 6000);
        spdlog::debug("Session with ID {} has been created", id);
    }

    net::awaitable<void> start() {  // 1. Prepare (Download files)
        co_await executor_->Prepare();

        // 2. Audio Loop
        auto next_tick = std::chrono::steady_clock::now();
        std::array<uint8_t, FRAME_SIZE_BYTES> pcm_buffer;

        for (;;) {
            // Timing Logic
            next_tick += std::chrono::milliseconds(20);
            timer_.expires_at(next_tick);
            co_await timer_.async_wait(boost::asio::use_awaitable);

            // Execution Logic
            bool has_more = executor_->GetNextFrame(pcm_buffer);
            if (!has_more) {
                spdlog::info("Session {} finished.", id_);
                break;
            }

            // Network Logic
            streamer_->SendFrame(pcm_buffer);
        }
    }

    net::awaitable<void> stop() {
        // Stop logic...
        co_return;
    }
};
Session::Session(boost::asio::io_context& io, std::string id, Graph&& g)
    : pImpl_(std::make_unique<Impl>(io, std::move(id), std::move(g))) {}

Session::~Session() = default;

net::awaitable<void> Session::start() {
    return pImpl_->start();
}

// Delegate stop()
net::awaitable<void> Session::stop() {
    return pImpl_->stop();
}
