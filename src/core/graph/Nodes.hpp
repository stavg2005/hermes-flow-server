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
 * @brief Thread-safe ping-pong buffer for async file reading.
 *
 * This structure manages two memory blocks:
 * - One is exposed for reading by the Audio Thread.
 * - The other is refilled asynchronously by the IO Thread.
 *
 * @invariant Only swap buffers when 'back_buffer_ready' is true to avoid underruns.
 */
struct Double_Buffer {
    /** @brief Path of the file being streamed; used for logging/debugging. */
    std::filesystem::path path;

    /**
     * @brief Flag indicating the background IO operation has completed.
     * Use memory_order_acquire when reading this flag.
     */
    std::atomic<bool> back_buffer_ready{false};

    /** @brief Constructor initializes both buffers to BUFFER_SIZE zeros. */
    Double_Buffer();

    // Disable copy/move for safety unless explicitly implemented
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
 * @brief Interface for nodes capable of producing audio frames.
 */
struct IAudioProcessor {
    /** @brief Process the next audio frame into the provided buffer. */
    virtual void ProcessFrame(std::span<uint8_t> frame_buffer) = 0;

    /** @brief Release resources and reset node state. */
    virtual void Close() = 0;

    virtual ~IAudioProcessor() = default;
};

/**
 * @brief Interface for nodes that require async initialization.
 */
struct IAsyncInitializer {
    /** @brief Initialize internal buffers asynchronously. */
    virtual asio::awaitable<void> InitializeBuffers() = 0;
    virtual ~IAsyncInitializer() = default;
};

//  Base Node

/**
 * @brief Base class for all nodes in the audio graph.
 *
 * Holds execution state and links to the next node (`target`).
 */
struct Node {
    std::string id;
    NodeKind kind;
    Node* target = nullptr;

    // Execution State
    int processed_frames{0};           /**< Frames processed so far */
    int total_frames{0};               /**< Total frames this node will output */
    int in_buffer_processed_frames{0}; /**< Frames processed in the current buffer */

    /** @brief Constructs a node and optionally links to a target. */
    explicit Node(Node* t = nullptr);

    virtual ~Node() = default;

    /** @brief Returns this node as an audio processor, if applicable. */
    virtual IAudioProcessor* AsAudio();
};


//  Concrete Nodes


/**
 * @brief Holds configuration options for FileInput nodes, e.g., gain adjustment.
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

    /**
     * @brief Threshold (in frames) to trigger back-buffer refill.
     * When read pointer passes this threshold, an async refill is dispatched.
     */
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
     * @details Used at startup to preload buffers. Ensures `back_buffer_ready` is true
     * before swapping to avoid underruns.
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
     * @brief Apply audio effects (e.g., gain) to the current frame buffer.
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
    /** @brief Sets total_frames to the max among inputs. */
    void SetMaxFrames();

    /** @brief Add a FileInputNode as an input to the mixer. */
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
    std::unordered_map<std::string, uint16_t> clients; /**< IP -> Port map */

    explicit ClientsNode(Node* t = nullptr);

    /** @brief Register a new client to receive audio. */
    void AddClient(std::string ip, uint16_t port);
};



// Graph
/**
 * @brief Represents the "AST" of an audio workflow.
 *
 * A Graph is a collection of Nodes linked by `target` pointers.
 *
 * **Structure:**
 * - `nodes`: Ownership container holding shared_ptr to nodes.
 * - `node_map`: Fast lookup (ID -> Node*) for linking edges during parsing.
 * - `start_node`: Entry point for execution (usually a FileInput or Mixer).
 *
 * **Execution Flow:**
 * AudioExecutor starts at `start_node` and follows `target` pointers
 * frame-by-frame.
 */
struct Graph {
    std::vector<std::shared_ptr<Node>> nodes; /**< Node ownership container */
    std::unordered_map<std::string, std::shared_ptr<Node>> node_map; /**< Fast lookup map */
    Node* start_node = nullptr;                                      /**< Execution entry point */
};
