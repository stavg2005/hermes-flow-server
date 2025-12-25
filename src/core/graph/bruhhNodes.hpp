#pragma once
#include <algorithm>
#include <atomic>
#include <boost/asio.hpp>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "WavUtils.hpp"
#include "config.hpp"
#include "spdlog/spdlog.h"

// Forward declaration needed for pointers
struct Node;

// --- Helper Structs ---
struct Double_Buffer {
    std::filesystem::path path;
    std::atomic<bool> back_buffer_ready{false};

   private:
    std::array<std::vector<uint8_t>, 2> blocks_;
    int read_index_ = 0;

   public:
    Double_Buffer() {
        blocks_[0].resize(BUFFER_SIZE, 0);
        blocks_[1].resize(BUFFER_SIZE, 0);
    }

    // --- CRITICAL FIX: Manual Move Semantics ---

    // 1. Move Constructor
    Double_Buffer(Double_Buffer&& other) noexcept {
        path = std::move(other.path);
        blocks_ = std::move(other.blocks_);
        read_index_ = other.read_index_;
        // Atomically move the bool flag
        back_buffer_ready.store(other.back_buffer_ready.load());
        other.back_buffer_ready.store(false);
    }

    // 2. Move Assignment
    Double_Buffer& operator=(Double_Buffer&& other) noexcept {
        if (this != &other) {
            path = std::move(other.path);
            blocks_ = std::move(other.blocks_);
            read_index_ = other.read_index_;
            back_buffer_ready.store(other.back_buffer_ready.load());
            other.back_buffer_ready.store(false);
        }
        return *this;
    }

    // 3. Delete Copy (Atomic cannot be copied)
    Double_Buffer(const Double_Buffer&) = delete;
    Double_Buffer& operator=(const Double_Buffer&) = delete;

    // Methods
    std::span<uint8_t> GetReadSpan() { return std::span(blocks_[read_index_].data(), BUFFER_SIZE); }
    std::span<uint8_t> GetWriteSpan() { return std::span(blocks_[read_index_ ^ 1]); }

    void Swap() {
        read_index_ ^= 1;
        back_buffer_ready = false;
    }

    void set_read_index(int value) {
        if (value == 0 || value == 1) read_index_ = value;
    }
};

// --- Node Logic Structures (No Inheritance) ---

struct FileInput {
    // Fields
    std::string file_name;
    std::string file_path;
    Double_Buffer bf;
    boost::asio::stream_file file_handle;

    // State
    bool is_first_read = true;
    size_t offset_size = WAV_HEADER_SIZE;
    int in_buffer_procced_frames = 0;

    // Config (Simplified: Store gain directly)
    double gain = 1.0;

    // Constructors
    // Note: We need a reference to IO context to init the file handle
    FileInput(boost::asio::io_context& io, std::string name, std::string path);

    // Methods
    void ProcessFrame(std::span<uint8_t> frame_buffer, Node& parent);
    boost::asio::awaitable<void> RequestRefillAsync(std::shared_ptr<Node> self_node);
    boost::asio::awaitable<void> InitilizeBuffers(int& total_frames_out);
    void Open(int& total_frames_out);
    void Close(Node& parent);
    void ApplyEffects(std::span<uint8_t> frame_buffer);
};

struct Mixer {
    // Inputs are pointers to the Wrapper Nodes
    std::vector<FileInput*> inputs;
    std::array<int32_t, SAMPLES_PER_FRAME> accumulator_;
    // Temp buffer for mixing
    std::array<uint8_t, FRAME_SIZE_BYTES> temp_input_buffer_;

    void ProcessFrame(std::span<uint8_t> buffer, Node& parent);
    void SetMaxFrames(int& total_frames_out);
    void Close(Node& parent);
};

struct Delay {
    float delay_ms = 0.0f;
    void ProcessFrame(std::span<uint8_t> buffer, Node& parent);
    void Close(Node& parent);
};

struct Clients {
    std::unordered_map<std::string, uint16_t> clients;
    void AddClient(std::string ip, uint16_t port);
};

struct FileOptions {
    double gain = 1.0;
};

// --- Master Node Wrapper ---

// 1. Define the Variant
using NodeData = std::variant<FileInput, Mixer, Delay, Clients, FileOptions>;

// 2. Define the Wrapper
struct Node : std::enable_shared_from_this<Node> {
    std::string id;
    Node* target = nullptr;

    // Common Execution State
    int processed_frames = 0;
    int total_frames = 0;

    // The Logic
    NodeData data;

    explicit Node(std::string id, NodeData&& d) : id(std::move(id)), data(std::move(d)) {}
};

struct Graph {
    // We hold the nodes alive here
    std::vector<std::shared_ptr<Node>> nodes;

    // Quick lookup by ID
    std::unordered_map<std::string, std::shared_ptr<Node>> node_map;

    // Entry point for the executor
    Node* start_node = nullptr;
};
