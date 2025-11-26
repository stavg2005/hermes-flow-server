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
      transmitter_(io_, "host.docker.internal", 6000) {
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
    try {
        for (;;) {
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
    std::span<uint8_t, FRAME_SIZE_BYTES> packet_span(packet_buffer.data(), FRAME_SIZE_BYTES);

    auto packet_size = PacketUtils::packet2rtp(frame_buffer, packetizer_, packet_span);
    has_frame_ = packet_size > 0;

    if (has_frame_) {
        transmitter_.asyncSend(packet_span, packet_size);
    }

    co_return;
}
