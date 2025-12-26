#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>
#include <array>

#include <boost/asio.hpp>
#include "config.hpp"
#include "types.hpp"


// =========================================================
//  Enums
// =========================================================

enum class NodeKind {
    FileInput,
    Mixer,
    Delay,
    Clients,
    FileOptions
};

// =========================================================
//  Helpers
// =========================================================

struct Double_Buffer {
    std::filesystem::path path;
    std::atomic<bool> back_buffer_ready{false};

    Double_Buffer();

    // Disable copy/move for safety unless explicitly implemented
    Double_Buffer(const Double_Buffer&) = delete;
    Double_Buffer& operator=(const Double_Buffer&) = delete;

    std::span<uint8_t> GetReadSpan();
    std::span<uint8_t> GetWriteSpan();
    void set_read_index(int value);
    void Swap();

private:
    std::array<std::vector<uint8_t>, 2> blocks_;
    int read_index_ = 0;
};

// =========================================================
//  Interfaces
// =========================================================

struct IAudioProcessor {
    virtual void ProcessFrame(std::span<uint8_t> frame_buffer) = 0;
    virtual void Close() = 0;
    virtual ~IAudioProcessor() = default;
};

struct IAsyncInitializer {
    virtual asio::awaitable<void> InitializeBuffers() = 0;
    virtual ~IAsyncInitializer() = default;
};

// =========================================================
//  Base Node
// =========================================================

struct Node {
    std::string id;
    NodeKind kind;
    Node* target = nullptr;

    // Execution State
    int processed_frames{0};
    int total_frames{0};
    int in_buffer_processed_frames{0};

    explicit Node(Node* t = nullptr);
    virtual ~Node() = default;

    virtual IAudioProcessor* AsAudio();
};

// =========================================================
//  Concrete Nodes
// =========================================================

struct FileOptionsNode : Node {
    double gain{1.0};
    explicit FileOptionsNode(Node* t = nullptr);
};

struct FileInputNode : Node,
                       IAudioProcessor,
                       IAsyncInitializer,
                       std::enable_shared_from_this<FileInputNode> {

    std::string file_name;
    std::string file_path;
    const int refill_threshold_frames;

    Double_Buffer bf;
    asio::stream_file file_handle;
    bool is_first_read = true;
    size_t offset_size = 0;
    std::shared_ptr<FileOptionsNode> options;

    explicit FileInputNode(asio::io_context& io, std::string name, std::string path);

    // Overrides
    IAudioProcessor* AsAudio() override;
    void ProcessFrame(std::span<uint8_t> frame_buffer) override;
    void Close() override;
    asio::awaitable<void> InitializeBuffers() override;

    // Specific Methods
    void SetOptions(std::shared_ptr<FileOptionsNode> options_node);
    void Open();
    void ApplyEffects(std::span<uint8_t> frame_buffer);
    asio::awaitable<void> RequestRefillAsync();
};

struct MixerNode : Node, IAudioProcessor {
    std::vector<FileInputNode*> inputs;
    std::array<int32_t, SAMPLES_PER_FRAME> accumulator_{};
    std::array<uint8_t, FRAME_SIZE_BYTES> temp_input_buffer_{};

    explicit MixerNode(Node* t = nullptr);

    // Overrides
    IAudioProcessor* AsAudio() override;
    void ProcessFrame(std::span<uint8_t> frame_buffer) override;
    void Close() override;

    // Specific Methods
    void SetMaxFrames();
    void AddInput(FileInputNode* node);
};

struct DelayNode : Node, IAudioProcessor {
    float delay_ms{0.0f};

    explicit DelayNode(Node* t = nullptr);

    IAudioProcessor* AsAudio() override;
    void ProcessFrame(std::span<uint8_t> frame_buffer) override;
    void Close() override;
};

struct ClientsNode : Node {
    std::unordered_map<std::string, uint16_t> clients;

    explicit ClientsNode(Node* t = nullptr);
    void AddClient(std::string ip, uint16_t port);
};

// =========================================================
//  Graph
// =========================================================

struct Graph {
    std::vector<std::shared_ptr<Node>> nodes;
    std::unordered_map<std::string, std::shared_ptr<Node>> node_map;
    Node* start_node = nullptr;
};
