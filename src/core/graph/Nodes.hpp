#pragma once

#include <array>
#include <atomic>
#include <boost/asio.hpp>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "Config.hpp"
#include "Types.hpp"

// Enums
/**
 * @brief Type of node in the audio processing graph.
 */
enum class NodeKind { FileInput, Mixer, Delay, Clients, FileOptions };

// Helpers

/**
 * @brief Async double-buffer.
 * 'back_buffer_ready' must be true before swap.
 */
struct DoubleBuffer {
    std::filesystem::path path_;

    /**
     * @brief Flag indicating the async refill operation has completed.
     */
    std::atomic<bool> back_buffer_ready_{false};

    /** @brief Constructor initializes both buffers to BUFFER_SIZE zeros. */
    DoubleBuffer();

    DoubleBuffer(const DoubleBuffer&) = delete;
    DoubleBuffer& operator=(const DoubleBuffer&) = delete;

    /** @brief Returns a span pointing to the currently active read buffer. */
    std::span<uint8_t> get_read_span();

    /** @brief Returns a span pointing to the inactive write buffer. */
    std::span<uint8_t> get_write_span();

    /** @brief Sets the index of the read buffer (0 or 1). */
    void set_read_index(int value);

    /**
     * @brief Swaps the read and write buffers.
     * @note Ensure 'back_buffer_ready' is true before calling to avoid underrun.
     */
    void swap();

   private:
    std::array<std::vector<uint8_t>, 2> blocks_;
    int read_index_ = 0;
};

// Interfaces
/**
 * @brief Interface for nodes that can produce audio frames.
 */
struct IAudioProcessor {
    /** @brief Process the next audio frame into the provided buffer. */
    virtual void process_frame(std::span<uint8_t> frame_buffer) = 0;

    /** @brief Release resources and reset node state. */
    virtual void close() = 0;

    virtual ~IAudioProcessor() = default;
};

/**
 * @brief Interface for nodes that need async initialization.
 */
struct IAsyncInitializer {
    virtual boost::asio::awaitable<void> initialize_buffers() = 0;
    virtual ~IAsyncInitializer() = default;
};

// Base Node

/**
 * @brief Base class for all nodes in the audio graph
 * Holds execution state and links to the next node.
 */
struct Node {
    std::string id_;
    NodeKind kind_;
    Node* target_ = nullptr;

    // Execution State
    int processed_frames_{0};           /**< Frames processed so far */
    int total_frames_{0};               /**< Total frames this node will output */
    int in_buffer_processed_frames_{0}; /**< Frames processed in the current buffer */

    explicit Node(Node* t = nullptr);
    virtual ~Node() = default;

    /** @brief Returns this node as an audio processor, if it is. */
    virtual IAudioProcessor* as_audio();
};

// Concrete Nodes

/**
 * @brief Holds configuration options for FileInput nodes like gain adjustment.
 */
struct FileOptionsNode : Node {
    double gain{1.0};
    explicit FileOptionsNode(Node* t = nullptr);
};

/**
 * @brief Streams audio from disk using non-blocking I/O.
 */
struct FileInputNode : Node,
                       IAudioProcessor,
                       IAsyncInitializer,
                       std::enable_shared_from_this<FileInputNode> {
    std::string file_name_;
    std::string file_path_;

    const int refill_threshold_frames_;

    DoubleBuffer bf_;
    boost::asio::stream_file file_handle_;
    bool is_first_read_ = true;
    size_t offset_size_ = 0;
    std::shared_ptr<FileOptionsNode> options_;

    explicit FileInputNode(boost::asio::io_context& io, std::string name, std::string path);

    // Overrides
    IAudioProcessor* as_audio() override;
    void process_frame(std::span<uint8_t> frame_buffer) override;
    void close() override;

    /**
     * @brief Fill both buffers of the file input asynchronously.
     */
    boost::asio::awaitable<void> initialize_buffers() override;

    // Specific Methods
    /**
     * @brief Attach a FileOptionsNode to this FileInput for gain/effect adjustments.
     */
    void set_options(std::shared_ptr<FileOptionsNode> options_node);

    /** @brief Open the file on disk and compute total frames. */
    void open();

    /**
     * @brief Apply audio effects to the current frame buffer.
     * @param frame_buffer Buffer to modify in-place.
     */
    void apply_effects(std::span<uint8_t> frame_buffer);

    /**
     * @brief Refill the back buffer asynchronously.
     */
    boost::asio::awaitable<void> request_refill_async();
};

/**
 * @brief Mixes multiple FileInputNode sources into a single audio stream.
 */
struct MixerNode : Node, IAudioProcessor {
    std::vector<FileInputNode*> inputs_;
    std::array<int32_t, SAMPLES_PER_FRAME> accumulator_{};      /**< Intermediate mix buffer */
    std::array<uint8_t, FRAME_SIZE_BYTES> temp_input_buffer_{}; /**< Temp buffer for input frames */

    explicit MixerNode(Node* t = nullptr);

    // Overrides
    IAudioProcessor* as_audio() override;
    void process_frame(std::span<uint8_t> frame_buffer) override;
    void close() override;

    // Specific Methods
    void set_max_frames();
    void add_input(FileInputNode* node);
};

/**
 * @brief Inserts silence or delay into the audio stream.
 */
struct DelayNode : Node, IAudioProcessor {
    float delay_ms_{0.0F};

    explicit DelayNode(Node* t = nullptr);

    IAudioProcessor* as_audio() override;
    void process_frame(std::span<uint8_t> frame_buffer) override;
    void close() override;
};

/**
 * @brief Maintains a list of client endpoints for streaming audio.
 */
struct ClientsNode : Node {
    std::unordered_map<std::string, uint16_t> clients;

    explicit ClientsNode(Node* t = nullptr);

    void add_client(std::string ip, uint16_t port);
};

// Graph
struct Graph {
    std::vector<std::shared_ptr<Node>> nodes;
    std::unordered_map<std::string, std::shared_ptr<Node>> node_map;
    Node* start_node = nullptr;
};
