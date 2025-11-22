#pragma once

#include <algorithm>  // for std::fill
#include <array>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// Boost Includes
#include <boost/asio.hpp>
#include <boost/system/error_code.hpp>

// Local Includes
#include "boost/asio/awaitable.hpp"
#include "boost/asio/detached.hpp"
#include "config.hpp"
#include "spdlog/spdlog.h"

namespace asio = boost::asio;

static constexpr int RefillThreshold = 450;
// -------- Enums --------
enum class NodeKind { FileInput, Mixer, Delay, Clients, FileOptions };

// -------- Data Structures --------
struct Double_Buffer {
    std::filesystem::path path;
    double gain{1.0};

    // Private implementation details
   private:
    std::array<std::vector<uint8_t>, 2> blocks_;
    int read_index_ = 0;

   public:
    Double_Buffer() {
        // Pre-allocate both blocks to avoid allocation during runtime
        blocks_[0].resize(BUFFER_SIZE, 0);
        blocks_[1].resize(BUFFER_SIZE, 0);
    }

    // Returns the buffer the Audio Thread reads from
    std::span<uint8_t> GetReadSpan() { return blocks_[read_index_]; }

    // Returns the buffer the IO Thread writes to (the "Back" buffer)
    std::span<uint8_t> GetWriteSpan() {
        // XOR operator toggles between 0 and 1
        return blocks_[read_index_ ^ 1];
    }

    // Called by Audio Thread when it finishes reading a block
    void Swap() {
        read_index_ ^= 1;  // Toggles 0 -> 1, 1 -> 0
    }

    size_t Size() const { return BUFFER_SIZE; }
};

// -------- Base Node --------
struct Node {
    explicit Node(Node* t = nullptr) : target(t) {}
    virtual ~Node() = default;

    std::string id;
    NodeKind kind;
    Node* target;

    int processed_frames{0};
    int in_buffer_procced_frames{0};
    int total_frames{0};

    virtual void ProcessFrame(std::span<uint8_t> frame_buffer) = 0;
    virtual asio::awaitable<void> RequestRefillAsync() = 0;
    virtual asio::awaitable<void> InitilizeBuffers() = 0;
};

// -------- File Input Node --------
struct FileInputNode : Node, std::enable_shared_from_this<FileInputNode> {
    std::string file_name;
    std::string file_path;
    Double_Buffer bf;

    asio::stream_file file_handle;

    bool refill_pending = false;

    const int refill_threshold_frames;

    explicit FileInputNode(asio::io_context& io, std::string name, std::string path)
        : Node(nullptr),
          file_name(std::move(name)),
          file_path(std::move(path)),
          file_handle(io),
          refill_threshold_frames(BUFFER_SIZE / FRAME_SIZE / 2) {  // Default to half buffer

        kind = NodeKind::FileInput;
        bf.path = file_path;
    }

    void Open() {
        boost::system::error_code ec;
        file_handle.open(file_path, asio::file_base::read_only, ec);

        if (ec) {
            spdlog::error("[{}] Failed to open file {}: {}", id, file_path, ec.message());
            return;
        }

        if (file_handle.is_open()) {
            total_frames = file_handle.size() / FRAME_SIZE;
            spdlog::info("[{}] File opened. Total frames: {}", id, total_frames);
        }
    }

    asio::awaitable<void> InitilizeBuffers() override {
        // Ensure file is open
        if (!file_handle.is_open()) Open();

        // Fill the "Write" buffer (initially back buffer)
        co_await RequestRefillAsync();
    }

    void ProcessFrame(std::span<uint8_t> frame_buffer) override {
        // Get current read pointer
        auto current_span = bf.GetReadSpan();

        // Calculate offsets
        size_t buffer_offset = in_buffer_procced_frames * FRAME_SIZE;

        // Safety check for buffer overruns
        if (buffer_offset + FRAME_SIZE > current_span.size()) {
            std::fill(frame_buffer.begin(), frame_buffer.end(), 0);  // Output silence on error
            return;
        }

        // Copy audio data
        auto start_it = current_span.begin() + buffer_offset;
        std::copy(start_it, start_it + FRAME_SIZE, frame_buffer.begin());

        // Update counters
        processed_frames++;
        spdlog::info("{}/{}", processed_frames, total_frames);
        in_buffer_procced_frames++;

        // Refill Logic
        // Check if we passed the threshold AND no refill is currently happening
        if (in_buffer_procced_frames >= refill_threshold_frames && !refill_pending) {
            refill_pending = true;

            co_spawn(
                file_handle.get_executor(),
                [this]() -> asio::awaitable<void> { co_await RequestRefillAsync(); },
                asio::detached);
        }

        // End of Buffer Logic
        // If we consumed the whole buffer, swap to the next one
        if (in_buffer_procced_frames >= (bf.Size() / FRAME_SIZE)) {
            bf.Swap();
            in_buffer_procced_frames = 0;
        }
    }

    // -------- IO PATH (Worker Thread) --------
    asio::awaitable<void> RequestRefillAsync() override {
        if (!file_handle.is_open()) co_return;

        // Get the "Back" buffer (the one not being read by audio thread)
        auto buffer_span = bf.GetWriteSpan();

        try {
            // Perform Async Read
            size_t bytes_read = co_await asio::async_read(file_handle, asio::buffer(buffer_span),
                                                          asio::use_awaitable);

            // Handle partial reads (EOF in middle of buffer) by zeroing the rest
            if (bytes_read < buffer_span.size()) {
                std::fill(buffer_span.begin() + bytes_read, buffer_span.end(), 0);
            }

        } catch (const boost::system::system_error& e) {
            if (e.code() == asio::error::eof) {
                // EOF reached: Fill with silence or loop logic
                std::fill(buffer_span.begin(), buffer_span.end(), 0);

            } else {
                spdlog::error("[{}] Async read error: {}", id, e.what());
            }
        }

        // Mark refill as complete so the audio thread can request again later
        refill_pending = false;
    }
};

// -------- Mixer Node --------
struct MixerNode : Node {
    std::vector<FileInputNode*> inputs;

    explicit MixerNode(Node* t = nullptr) : Node(t) { kind = NodeKind::Mixer; }

    void AddInput(FileInputNode* node) { inputs.push_back(node); }
    asio::awaitable<void> InitilizeBuffers() override { co_return; }
    void ProcessFrame(std::span<uint8_t> frame_buffer) override {
        // Summing logic
    }

    asio::awaitable<void> RequestRefillAsync() override {
        for (auto* input : inputs) {
            co_await input->RequestRefillAsync();
        }
    }
};

// -------- Other Nodes --------
struct DelayNode : Node {
    int delay_ms{0};
    explicit DelayNode(Node* t = nullptr) : Node(t) { kind = NodeKind::Delay; }
    void ProcessFrame(std::span<uint8_t> frame_buffer) override {}
    asio::awaitable<void> RequestRefillAsync() override { co_return; }
    asio::awaitable<void> InitilizeBuffers() override { co_return; }
};

struct ClientsNode : Node {
    explicit ClientsNode(Node* t = nullptr) : Node(t) { kind = NodeKind::Clients; }
    void ProcessFrame(std::span<uint8_t> frame_buffer) override {}
    asio::awaitable<void> RequestRefillAsync() override { co_return; }
    asio::awaitable<void> InitilizeBuffers() override { co_return; }
};

struct FileOptionsNode : Node {
    double gain{1.0};
    explicit FileOptionsNode(Node* t = nullptr) : Node(t) { kind = NodeKind::FileOptions; }
    void ProcessFrame(std::span<uint8_t> frame_buffer) override {}
    asio::awaitable<void> RequestRefillAsync() override { co_return; }
    asio::awaitable<void> InitilizeBuffers() override { co_return; }
};

// -------- Graph Container --------
struct Graph {
    std::vector<std::unique_ptr<Node>> nodes;
    std::unordered_map<std::string, Node*> node_map;
    Node* start_node = nullptr;
};
