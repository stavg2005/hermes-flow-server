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

#include "config.hpp"
#include "types.hpp"

//  Enums
/**
 * @brief Type of node in the audio processing graph.
 */
enum class NodeKind { FileInput, Mixer, Delay, Clients, FileOptions };

//  Helpers

/**
 * @brief Async double-buffer.
 'back_buffer_ready' must be true before swap.
 */
struct Double_Buffer {
\
    std::filesystem::path path;

    /**
     * @brief Flag indicating the async refill operation has completed.
     */
    std::atomic<bool> back_buffer_ready{false};

    /** @brief Constructor initializes both buffers to BUFFER_SIZE zeros. */
    Double_Buffer();


    Double_Buffer(const Double_Buffer&) = delete;
    Double_Buffer& operator=(const Double_Buffer&) = delete;

    /** @brief Returns a span pointing to the currently active read buffer. */
    std::span<uint8_t> GetReadSpan();

    /** @brief Returns a span pointing to the inactive write buffer. */
    std::span<uint8_t> GetWriteSpan();

    /** @brief Sets the index of the read buffer (0 or 1). */
    void set_read_index(int value);

    /**
     * @brief Swaps the read and write buffers.
     * @note Ensure 'back_buffer_ready' is true before calling to avoid underrun.
     */
    void Swap();

   private:
    std::array<std::vector<uint8_t>, 2> blocks_;
    int read_index_ = 0;
};

//  Interfaces
/**
 * @brief Interface for nodes that can  producing audio frames.
 */
struct IAudioProcessor {
    /** @brief Process the next audio frame into the provided buffer. */
    virtual void ProcessFrame(std::span<uint8_t> frame_buffer) = 0;

    /** @brief Release resources and reset node state. */
    virtual void Close() = 0;

    virtual ~IAudioProcessor() = default;
};

/**
 * @brief Interface for nodes that need async initialization.
 */
struct IAsyncInitializer {

    virtual asio::awaitable<void> InitializeBuffers() = 0;
    virtual ~IAsyncInitializer() = default;
};

//  Base Node

/**
 * @brief Base class for all nodes in the audio graph
 * Holds execution state and links to the next node.
 */
struct Node {
    std::string id;
    NodeKind kind;
    Node* target = nullptr;

    // Execution State
    int processed_frames{0};           /**< Frames processed so far */
    int total_frames{0};               /**< Total frames this node will output */
    int in_buffer_processed_frames{0}; /**< Frames processed in the current buffer */


    explicit Node(Node* t = nullptr);

    virtual ~Node() = default;

    /** @brief Returns this node as an audio processor, if it is. */
    virtual IAudioProcessor* AsAudio();
};

//  Concrete Nodes

/**
 * @brief Holds configuration options for FileInput nodes like gain adjustment.
 */
struct FileOptionsNode : Node {
    double gain{1.0}; /**< Gain multiplier applied to audio samples */
    explicit FileOptionsNode(Node* t = nullptr);
};

/**
 * @brief Streams audio from disk using non-blocking I/O.
 *
 * Acts as a source node. Maintains a double-buffer system to ensure
 * the audio processing loop is never blocked by disk latency.
 */
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

    /**
     * @brief Fill both buffers of the file input asynchronously.
     */
    asio::awaitable<void> InitializeBuffers() override;

    // Specific Methods
    /**
     * @brief Attach a FileOptionsNode to this FileInput for gain/effect adjustments.
     */
    void SetOptions(std::shared_ptr<FileOptionsNode> options_node);

    /** @brief Open the file on disk and compute total frames. */
    void Open();

    /**
     * @brief Apply audio effects  to the current frame buffer.
     * @param frame_buffer Buffer to modify in-place.
     */
    void ApplyEffects(std::span<uint8_t> frame_buffer);

    /**
     * @brief Refill the back buffer asynchronously.
     * @details Detached coroutine. Zero-fills remainder if EOF reached.
     */
    asio::awaitable<void> RequestRefillAsync();
};

/**
 * @brief Mixes multiple FileInputNode sources into a single audio stream.
 */
struct MixerNode : Node, IAudioProcessor {
    std::vector<FileInputNode*> inputs;
    std::array<int32_t, SAMPLES_PER_FRAME> accumulator_{};      /**< Intermediate mix buffer */
    std::array<uint8_t, FRAME_SIZE_BYTES> temp_input_buffer_{}; /**< Temp buffer for input frames */

    explicit MixerNode(Node* t = nullptr);

    // Overrides
    IAudioProcessor* AsAudio() override;
    void ProcessFrame(std::span<uint8_t> frame_buffer) override;
    void Close() override;

    // Specific Methods

    void SetMaxFrames();


    void AddInput(FileInputNode* node);
};

/**
 * @brief Inserts silence or delay into the audio stream.
 */
struct DelayNode : Node, IAudioProcessor {
    float delay_ms{0.0f};

    explicit DelayNode(Node* t = nullptr);

    IAudioProcessor* AsAudio() override;
    void ProcessFrame(std::span<uint8_t> frame_buffer) override;
    void Close() override;
};

/**
 * @brief Maintains a list of client endpoints for streaming audio.
 */
struct ClientsNode : Node {
    std::unordered_map<std::string, uint16_t> clients;

    explicit ClientsNode(Node* t = nullptr);


    void AddClient(std::string ip, uint16_t port);
};

// Graph
// Audio graph container. Holds nodes and the execution entry point.
struct Graph {
    std::vector<std::shared_ptr<Node>> nodes;
    std::unordered_map<std::string, std::shared_ptr<Node>> node_map;
    Node* start_node = nullptr;
};
