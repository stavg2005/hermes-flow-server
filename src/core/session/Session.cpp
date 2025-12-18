#include "Session.hpp"

#include <boost/asio.hpp>
#include <memory>

#include "AudioExecutor.hpp"
#include "ISessionObserver.hpp"
#include "Nodes.hpp"
#include "RTPStreamer.hpp"

namespace net = boost::asio;

struct Session::Impl {
    boost::asio::io_context& io_;
    std::string id_;
    std::unique_ptr<AudioExecutor> executor_;
    std::unique_ptr<RTPStreamer> streamer_;
    net::steady_timer timer_;
    std::shared_ptr<Graph> graph_;
    std::shared_ptr<ISessionObserver> observer_;

    Impl(boost::asio::io_context& io, std::string id, Graph&& g)
        : io_(io), id_(std::move(id)), timer_(io), graph_(std::make_shared<Graph>(std::move(g))) {
        executor_ = std::make_unique<AudioExecutor>(io_, graph_);

        streamer_ = std::make_unique<RTPStreamer>(io_);
        spdlog::debug("Session with ID {} has been created", id);
    }

    void ConfigureStreamerFromGraph() {
        for (const auto& node : graph_->nodes) {
            // Use your kind check or dynamic_cast
            if (node->kind == NodeKind::Clients) {
                // Safe cast
                auto* clientsNode = static_cast<ClientsNode*>(node.get());

                // Iterate the map and register with streamer
                for (const auto& [ip, port] : clientsNode->clients) {
                    spdlog::info("Registering initial client: {}:{}", ip, port);
                    streamer_->AddClient(ip, port);
                }
            }
        }
    }

    void AttachObserver(std::shared_ptr<ISessionObserver> observer) {
        observer_ = std::move(observer);
    }
    net::awaitable<void> start() {  // 1. Prepare (Download files)
        co_await executor_->Prepare();
        ConfigureStreamerFromGraph();
        // 2. Audio Loop
        auto next_tick = std::chrono::steady_clock::now();
        std::array<uint8_t, FRAME_SIZE_BYTES> pcm_buffer;
        auto last_update_time_ = std::chrono::steady_clock::now();
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
            auto now = std::chrono::steady_clock::now();
            if (now - last_update_time_ > std::chrono::milliseconds(100)) {
                observer_->OnStatsUpdate(executor_->get_stats());
                last_update_time_ = now;
            }
            // Network Logic
            streamer_->SendFrame(pcm_buffer);
        }
    }

    void AddClient(std::string ip, uint16_t port) {
        // Just forward to the streamer
        streamer_->AddClient(ip, port);
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

void Session::AttachObserver(std::shared_ptr<ISessionObserver> observer) {
    pImpl_->AttachObserver(std::move(observer));
}

void Session::AddClient(std::string& ip, uint16_t port) {
    pImpl_->AddClient(ip, port);
}
