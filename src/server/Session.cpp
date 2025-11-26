#include "Session.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <chrono>
#include <exception>
#include <memory>

#include "Nodes.hpp"
#include "PacketUtils.hpp"
#include "RTPPacketizer.hpp"
#include "S3Session.hpp"
#include "boost/asio/awaitable.hpp"
#include "boost/asio/io_context.hpp"
#include "config.hpp"
#include "packet.hpp"
#include "spdlog/spdlog.h"

namespace net = boost::asio;

Session::Session(boost::asio::io_context& ex, std::string&& id, Graph&& g)
    : io_(ex),
      id_(std::move(id)),
      graph_(std::make_unique<Graph>(std::move(g))),
      timer_(ex),
      packetizer_(PAYLOAD_TYPE, generate_ssrc(), SAMPLES_PER_FRAME),
      transmitter_(io_, "127.0.0.1", 6000) {
    spdlog::debug("Session with ID {} has been created", id);
};

Node* Session::get_start_node() {
    if (graph_->start_node == nullptr) {
        throw std::runtime_error("Graph has no valid start_node set.");
    }
    return graph_->start_node;
}

bool FileExist(std::string& file_name) {
    // TODO actually check if file already in directory
    return false;
}

net::awaitable<void> Session::FetchFiles() {
    spdlog::info("fetching fiiles for Session ID {}", id_);
    for (const auto& node : graph_->nodes) {
        spdlog::info("salsa ipicante");
        if (node->kind == NodeKind::FileInput) {
            try {
                spdlog::info("file-input found", id_);
                auto* FileInputPtr = dynamic_cast<FileInputNode*>(node.get());
                if (!FileInputPtr) {
                    spdlog::critical("cast faild for node {}", node->id);
                    break;
                }
                if (FileExist(FileInputPtr->file_name)) {
                    break;
                }
                auto session = std::make_shared<S3Session>(io_);
                co_await session->RequestFile(FileInputPtr->file_name);
            } catch (std::exception ec) {
                spdlog::error("an exception has occoured while fetching files {}", ec.what());
            }
        }
    }
}

net::awaitable<void> Session::start() {
    co_await FetchFiles();

    spdlog::info("fetched files for Session ID {}", id_);

    current_node = get_start_node();
    co_await current_node->InitilizeBuffers();
    spdlog::info("current node {}", current_node->id);
    auto next_tick_time = std::chrono::steady_clock::now();
    const auto interval = std::chrono::milliseconds(20);
    bool first_run = true;
    try {
        for (;;) {
            auto now = std::chrono::steady_clock::now();

            // ----------------------------------------------------------------
            // 1. LAG DETECTION (The Fix)
            // ----------------------------------------------------------------
            if (first_run) {
                next_tick_time = now;
                first_run = false;
            } else if (now > next_tick_time + std::chrono::milliseconds(100)) {
                // We are >100ms behind schedule.
                // If we continue, we will burst 5+ packets instantly.
                // STOP. Reset the clock.
                spdlog::warn("[Session] System lag detected! Skipping missed ticks.");
                next_tick_time = now;
            }

            next_tick_time += interval;
            std::span<uint8_t, FRAME_SIZE_BYTES> frame_buffer_(frame_data_.data(),
                                                               FRAME_SIZE_BYTES);
            // process the frame into the buffer
            co_await Process_Current_Node_Frame(frame_buffer_);

            co_await Packetize_And_Transmit_Frame(frame_buffer_);
            timer_.expires_at(next_tick_time);
            co_await timer_.async_wait(net::use_awaitable);
        }
    } catch (const boost::system::system_error& e) {
        if (e.code() == net::error::operation_aborted) {
        } else {
            spdlog::error("exception occured in Session ID: {} ", e.what());
        }
    }
}

net::awaitable<void> Session::Process_Current_Node_Frame(
    std::span<uint8_t, FRAME_SIZE_BYTES> frame_buffer) {
    current_node->ProcessFrame(frame_buffer);

    // Check if this frame was the last one for this node.
    if (current_node->processed_frames >= current_node->total_frames) {
        current_node = current_node->target;
        spdlog::info("new current  node {}", current_node->id);
        co_return;
    }

    co_return;
}

net::awaitable<void> Session::Packetize_And_Transmit_Frame(
    std::span<uint8_t, FRAME_SIZE_BYTES> frame_buffer) {
    // 1. ALLOCATE: Create a container that owns its own memory
    // (Using make_shared is efficient and usually performs 1 allocation)
    auto packet_owner = std::make_shared<std::vector<uint8_t>>(FRAME_SIZE_BYTES);

    // 2. WRAP: Create a span so your existing code can write into it
    std::span<uint8_t> packet_span(packet_owner->data(), FRAME_SIZE_BYTES);

    // 3. WRITE: Write audio directly into the final memory (Zero Copy!)
    // Your packet2rtp writes directly into packet_owner's memory via the span
    auto packet_size = PacketUtils::packet2rtp(frame_buffer, packetizer_, packet_span);

    has_frame_ = packet_size > 0;

    if (has_frame_) {
        // 4. TRANSFER: specific ownership to the transmitter
        // No memcpy happens here. We just pass the pointer.
        transmitter_.asyncSend(std::move(packet_owner), packet_size);
    }

    co_return;
}
