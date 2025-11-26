// src/models/Nodes.hpp
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <boost/asio.hpp>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "config.hpp"  //
#include "boost/asio/awaitable.hpp"
#include "spdlog/spdlog.h"


namespace asio = boost::asio;

// -------- Enums --------
enum class NodeKind { FileInput, Mixer, Delay, Clients, FileOptions };

// -------- Data Structures --------
struct Double_Buffer {
    std::filesystem::path path;
    double gain{1.0};
    std::atomic<bool> back_buffer_ready{false};

   private:
    std::array<std::vector<uint8_t>, 2> blocks_;
    int read_index_ = 0;

   public:
    Double_Buffer() {
        blocks_[0].resize(BUFFER_SIZE, 0);  //
        blocks_[1].resize(BUFFER_SIZE, 0);
    }

    std::span<uint8_t> GetReadSpan() {
        return std::span(blocks_[read_index_].data() + WAV_HEADER_SIZE,
                         BUFFER_SIZE - WAV_HEADER_SIZE);
    }

    std::span<uint8_t> GetWriteSpan() { return std::span(blocks_[read_index_ ^ 1]); }

    void set_read_index(int value) {
        if (value == 0 || value == 1) read_index_ = value;
    }

    void Swap() {
        read_index_ ^= 1;
        back_buffer_ready = false;
    }
};

// =========================================================
//  (ISP)
// =========================================================

// Interface for nodes that manipulate audio data
struct IAudioProcessor {
    virtual void ProcessFrame(std::span<uint8_t> frame_buffer) = 0;
    virtual ~IAudioProcessor() = default;
};

// Interface for nodes that require asynchronous initialization
struct IAsyncInitializer {
    virtual asio::awaitable<void> InitilizeBuffers() = 0;
    virtual ~IAsyncInitializer() = default;
};

// BASE NODE

struct Node {
    explicit Node(Node* t = nullptr) : target(t) {}
    virtual ~Node() = default;
    //Virtual Cast idiom
    virtual IAudioProcessor* AsAudio() { return nullptr; }
    std::string id;
    NodeKind kind;
    Node* target;

    // Execution state used by the Session loop
    int processed_frames{0};
    int total_frames{0};
    int in_buffer_procced_frames{0};
};




// File Input Node (Needs Audio + Async Init)
struct FileInputNode : Node,
                       IAudioProcessor,
                       IAsyncInitializer,
                       std::enable_shared_from_this<FileInputNode> {
    std::string file_name;
    std::string file_path;
    Double_Buffer bf;
    asio::stream_file file_handle;
    const int refill_threshold_frames;

    explicit FileInputNode(asio::io_context& io, std::string name, std::string path)
        : Node(nullptr),
          file_name(std::move(name)),
          file_path(std::move(path)),
          file_handle(io),
          refill_threshold_frames(BUFFER_SIZE / FRAME_SIZE_BYTES / 2) {
        kind = NodeKind::FileInput;
        bf.path = file_path;
    }

    IAudioProcessor* AsAudio() override { return this; }


    void Open() {
        boost::system::error_code ec;
        file_handle.open(file_path, asio::file_base::read_only, ec);
        if (!ec && file_handle.is_open()) {
            total_frames = file_handle.size() / FRAME_SIZE_BYTES;
        }
    }

    // --- IAsyncInitializer Implementation ---
    asio::awaitable<void> InitilizeBuffers() override {
        if (!file_handle.is_open()) Open();

        // Initial double-buffer fill
        bf.set_read_index(1);
        co_await RequestRefillAsync();
        bf.back_buffer_ready = true;
        bf.Swap();
        co_await RequestRefillAsync();
        bf.back_buffer_ready = true;
    }

    // --- IAudioProcessor Implementation ---
    void ProcessFrame(std::span<uint8_t> frame_buffer) override {
        auto current_span = bf.GetReadSpan();
        size_t buffer_offset = in_buffer_procced_frames * FRAME_SIZE_BYTES;

        if (buffer_offset + FRAME_SIZE_BYTES > current_span.size()) {
            if (!bf.back_buffer_ready) {
                std::fill(frame_buffer.begin(), frame_buffer.end(), 0);  // Underrun
                return;
            }
            bf.Swap();
            in_buffer_procced_frames = 0;
            current_span = bf.GetReadSpan();
            buffer_offset = 0;

            // Trigger refill for the background buffer
            asio::co_spawn(
                file_handle.get_executor(),
                [self = shared_from_this()]() { return self->RequestRefillAsync(); },
                asio::detached);
        }

        auto start_it = current_span.begin() + buffer_offset;
        std::copy(start_it, start_it + FRAME_SIZE_BYTES, frame_buffer.begin());

        in_buffer_procced_frames++;
        processed_frames++;
    }

    // Helper for internal use
    asio::awaitable<void> RequestRefillAsync() {
        if (!file_handle.is_open()) co_return;
        auto buffer_span = bf.GetWriteSpan();

        try {
            size_t bytes_read = co_await asio::async_read(file_handle, asio::buffer(buffer_span),
                                                          asio::use_awaitable);
            if (bytes_read < buffer_span.size()) {
                std::fill(buffer_span.begin() + bytes_read, buffer_span.end(), 0);
            }
        } catch (...) {
            std::fill(buffer_span.begin(), buffer_span.end(), 0);
        }
        bf.back_buffer_ready = true;
    }
};

// Mixer Node (Needs Audio + Async Init for inputs)
struct MixerNode : Node, IAudioProcessor, IAsyncInitializer {
    std::vector<FileInputNode*> inputs;  // Dependency on FileInput

    explicit MixerNode(Node* t = nullptr) : Node(t) { kind = NodeKind::Mixer; }

    void AddInput(FileInputNode* node) { inputs.push_back(node); }

    // Recursive Initialization
    asio::awaitable<void> InitilizeBuffers() override {
        for (auto* input : inputs) {
            co_await input->InitilizeBuffers();
        }
    }

    // Audio Processing
    void ProcessFrame(std::span<uint8_t> frame_buffer) override {
       //TODO implement mixing logic
        std::fill(frame_buffer.begin(), frame_buffer.end(), 0);
    }
};

// 3. Delay Node (Audio only, no Async IO)
struct DelayNode : Node, IAudioProcessor {
    int delay_ms{0};
    explicit DelayNode(Node* t = nullptr) : Node(t) { kind = NodeKind::Delay; }

    void ProcessFrame(std::span<uint8_t> frame_buffer) override {
        std::fill(frame_buffer.begin(), frame_buffer.end(), 0);
    }
};

// 4. Clients Node (Just identity or metadata)
struct ClientsNode : Node {
    explicit ClientsNode(Node* t = nullptr) : Node(t) { kind = NodeKind::Clients; }
};

// 5. File Options Node (Pure Config, NO Audio methods)
struct FileOptionsNode : Node {
    double gain{1.0};
    explicit FileOptionsNode(Node* t = nullptr) : Node(t) { kind = NodeKind::FileOptions; }
};

// -------- Graph Container --------
struct Graph {
    std::vector<std::shared_ptr<Node>> nodes;
    std::unordered_map<std::string, Node*> node_map;
    Node* start_node = nullptr;
};
