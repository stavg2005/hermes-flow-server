#include "Session.hpp"

#include <boost/asio.hpp>
#include <memory>
#include <variant>  // Required for std::get_if

#include "AudioExecutor.hpp"
#include "ISessionObserver.hpp"
#include "Nodes.hpp"  // Ensure this is your NEW header with the variant
#include "RTPStreamer.hpp"
#include "spdlog/spdlog.h"

namespace net = boost::asio;

struct Session::Impl {
    boost::asio::io_context& io_;
    std::string id_;
    std::unique_ptr<AudioExecutor> audio_executor_;
    std::unique_ptr<RTPStreamer> streamer_;
    net::steady_timer timer_;
    std::shared_ptr<Graph> graph_;
    std::shared_ptr<ISessionObserver> observer_;

    Impl(boost::asio::io_context& io, std::string id, Graph&& g)
        : io_(io), id_(std::move(id)), timer_(io), graph_(std::make_shared<Graph>(std::move(g))) {
        audio_executor_ = std::make_unique<AudioExecutor>(io_, graph_);
        streamer_ = std::make_unique<RTPStreamer>(io_);
        spdlog::debug("Session with ID {} has been created", id);
    }

    // -rtp stramer need to know the clients so this function grabs the client node and adds all the clients to the streamer-
    void ConfigureStreamerFromGraph() {
        for (const auto& node : graph_->nodes) {
            // kind check or dynamic_cast
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
    // -------------------------

    void AttachObserver(std::shared_ptr<ISessionObserver> observer) {
        spdlog::info("attached observer");
        observer_ = std::move(observer);
    }

    net::awaitable<void> start() {
        spdlog::info("starting session");
        // This works because AudioExecutor::Prepare is an awaitable
        co_await audio_executor_->Prepare();

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
            bool has_more = audio_executor_->GetNextFrame(pcm_buffer);

            if (!has_more) {
                spdlog::info("Session {} finished.", id_);
                break;
            }

            auto now = std::chrono::steady_clock::now();
            if (now - last_update_time_ > std::chrono::milliseconds(100)) {
                observer_->OnStatsUpdate(audio_executor_->get_stats());
                last_update_time_ = now;
            }

            // Network Logic
            streamer_->SendFrame(pcm_buffer);
        }
    }

    void AddClient(const std::string& ip, uint16_t port) const { streamer_->AddClient(ip, port); }

    net::awaitable<void> stop() { co_return; }
};

Session::Session(boost::asio::io_context& io, std::string id, Graph&& g)
    : pImpl_(std::make_unique<Impl>(io, std::move(id), std::move(g))) {}

Session::~Session() = default;

net::awaitable<void> Session::start() {
    return pImpl_->start();
}

net::awaitable<void> Session::stop() {
    return pImpl_->stop();
}

void Session::AttachObserver(std::shared_ptr<ISessionObserver> observer) {
    pImpl_->AttachObserver(std::move(observer));
}

void Session::AddClient(std::string& ip, uint16_t port) {
    pImpl_->AddClient(ip, port);
}
