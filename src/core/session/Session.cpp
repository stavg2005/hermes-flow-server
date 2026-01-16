#include "Session.hpp"

#include <spdlog/spdlog.h>

#include <array>
#include <boost/asio.hpp>
#include <chrono>
#include <memory>
#include <string>

#include "AudioExecutor.hpp"
#include "ISessionObserver.hpp"
#include "Nodes.hpp"
#include "RTPStreamer.hpp"

Session::Session(net::io_context& io, std::string id, Graph&& g)
    : io_(io), id_(std::move(id)), timer_(io), graph_(std::make_shared<Graph>(std::move(g))) {
    audio_executor_ = std::make_unique<AudioExecutor>(io_, graph_);
    streamer_ = std::make_unique<RTPStreamer>(io_);

    spdlog::debug("Session [{}] created.", id_);
}

void Session::AttachObserver(std::shared_ptr<ISessionObserver> observer) {
    observer_ = std::move(observer);
    spdlog::info("[{}] Observer attached.", id_);
}

void Session::AddClient(const std::string& ip, uint16_t port) {
    streamer_->AddClient(ip, port);
}

void Session::stop() {
    spdlog::info("[{}] Stopping session...", id_);
    is_running_ = false;

    timer_.cancel();
}

// Initialize streamer with clients found in the graph
void Session::ConfigureStreamerFromGraph() {
    for (const auto& node : graph_->nodes) {
        if (node->kind_ == NodeKind::Clients) {
            auto* clientsNode = static_cast<ClientsNode*>(node.get());

            for (const auto& [ip, port] : clientsNode->clients) {
                spdlog::info("[{}] Auto-registering client: {}:{}", id_, ip, port);
                streamer_->AddClient(ip, port);
            }
        }
    }
}

net::awaitable<void> Session::start() {
    spdlog::info("[{}] Starting session execution...", id_);
    is_running_ = true;

    try {
        co_await audio_executor_->Prepare();
    } catch (const std::exception& e) {
        spdlog::error("[{}] Audio preparation failed: {}", id_, e.what());
        if (observer_) {
            observer_->OnError("Audio preparation failed: " + std::string(e.what()));
        }
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

        // 1. Timer Wait (20ms)
        next_tick += std::chrono::milliseconds(20);
        timer_.expires_at(next_tick);
        co_await timer_.async_wait(net::use_awaitable);

        // 2. Fetch Frame
        auto [should_continue, status] = audio_executor_->GetNextFrame(pcm_buffer);

        // 3. Error / Status Handling
        if (status != NodeError::Success) {
            if (status == NodeError::Underrun) {
                spdlog::warn("[{}] Underrun detected (inserting silence)", id_);
            } else if (status == NodeError::EndOfStream) {
                spdlog::debug("[{}] Node reached EndOfStream", id_);
            } else {
                spdlog::error("[{}] Critical error: {}", id_, to_string(status));

                if (observer_) {
                    observer_->OnError(std::string(to_string(status)));
                }
                // שגיאה קריטית מחייבת עצירה
                should_continue = false;
            }
        }

        // 4. Flow Control
        if (!should_continue) {
            spdlog::info("[{}] Session finished (End of Graph).", id_);
            if (observer_) {
                observer_->OnSessionComplete();
            }
            break;
        }

        // 5. Send Audio
        streamer_->SendFrame(pcm_buffer);

        // 6. Update Stats (Throttle to ~10Hz)
        if (observer_) {
            auto now = std::chrono::steady_clock::now();
            if (now - last_stats_time > std::chrono::milliseconds(100)) {
                observer_->OnStatsUpdate(audio_executor_->get_stats());
                last_stats_time = now;
            }
        }
    }

    is_running_ = false;
}
bool Session::get_is_running() const {
    return is_running_;
}

Session::~Session() = default;
