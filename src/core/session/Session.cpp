#include "Session.hpp"

#include <spdlog/spdlog.h>

#include <array>
#include <boost/asio.hpp>
#include <chrono>
#include <memory>

#include "AudioExecutor.hpp"
#include "ISessionObserver.hpp"
#include "Nodes.hpp"
#include "RTPStreamer.hpp"

// =========================================================
//  Session Implementation (PIMPL)
// =========================================================

struct Session::Impl {
    net::io_context& io_;
    std::string id_;
    std::atomic<bool> is_running_{true};
    std::shared_ptr<Graph> graph_;
    std::unique_ptr<AudioExecutor> audio_executor_;
    std::unique_ptr<RTPStreamer> streamer_;

    net::steady_timer timer_;
    std::shared_ptr<ISessionObserver> observer_;

    Impl(net::io_context& io, std::string id, Graph&& g)
        : io_(io), id_(std::move(id)), timer_(io), graph_(std::make_shared<Graph>(std::move(g))) {
        audio_executor_ = std::make_unique<AudioExecutor>(io_, graph_);
        streamer_ = std::make_unique<RTPStreamer>(io_);

        spdlog::debug("Session [{}] created.", id_);
    }

    void AttachObserver(std::shared_ptr<ISessionObserver> observer) {
        observer_ = std::move(observer);
        spdlog::info("[{}] Observer attached.", id_);
    }

    void AddClient(const std::string& ip, uint16_t port) { streamer_->AddClient(ip, port); }

    void stop() {
        spdlog::info("[{}] Stopping session...", id_);
        is_running_ = false;

        timer_.cancel();
    }

    // Initialize streamer with clients found in the graph
    void ConfigureStreamerFromGraph() {
        for (const auto& node : graph_->nodes) {
            if (node->kind == NodeKind::Clients) {
                
                auto* clientsNode = static_cast<ClientsNode*>(node.get());

                for (const auto& [ip, port] : clientsNode->clients) {
                    spdlog::info("[{}] Auto-registering client: {}:{}", id_, ip, port);
                    streamer_->AddClient(ip, port);
                }
            }
        }
    }

    net::awaitable<void> start() {
        spdlog::info("[{}] Starting session execution...", id_);

        try {
            co_await audio_executor_->Prepare();
        } catch (const std::exception& e) {
            spdlog::error("[{}] Audio preparation failed: {}", id_, e.what());
            stop();
            co_return;
        }
        ConfigureStreamerFromGraph();

        // Audio Processing Loop
        // 20ms Frame Duration
        auto next_tick = std::chrono::steady_clock::now();
        auto last_stats_time = std::chrono::steady_clock::now();
        std::array<uint8_t, FRAME_SIZE_BYTES> pcm_buffer;

        for (;;) {
            if (!is_running_) break;

            next_tick += std::chrono::milliseconds(20);
            timer_.expires_at(next_tick);
            co_await timer_.async_wait(net::use_awaitable);

            bool has_more = audio_executor_->GetNextFrame(pcm_buffer);

            if (!has_more) {
                spdlog::info("[{}] Session finished (End of Graph).", id_);
                break;
            }

            streamer_->SendFrame(pcm_buffer);

            if (observer_) {
                auto now = std::chrono::steady_clock::now();
                // Notify observer stats no more than every 100ms to avoid excessive
                // logging/callbacks
                if (now - last_stats_time > std::chrono::milliseconds(100)) {
                    observer_->OnStatsUpdate(audio_executor_->get_stats());
                    last_stats_time = now;
                }
            }
        }
    }
};

//  Session Wrapper
Session::Session(net::io_context& io, std::string id, Graph&& g)
    : pImpl_(std::make_unique<Impl>(io, std::move(id), std::move(g))) {}

Session::~Session() = default;

net::awaitable<void> Session::start() {
    return pImpl_->start();
}
void Session::stop() {
    return pImpl_->stop();
}

void Session::AttachObserver(std::shared_ptr<ISessionObserver> observer) {
    pImpl_->AttachObserver(std::move(observer));
}

void Session::AddClient(std::string& ip, uint16_t port) {
    pImpl_->AddClient(ip, port);
}
