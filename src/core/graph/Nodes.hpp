// src/models/Nodes.hpp
#pragma once

#include <WavUtils.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <boost/asio.hpp>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "boost/asio/awaitable.hpp"
#include "config.hpp"  //
#include "spdlog/spdlog.h"

namespace asio = boost::asio;

// -------- Enums --------
enum class NodeKind { FileInput, Mixer, Delay, Clients, FileOptions };

// -------- Data Structures --------
struct Double_Buffer {
    std::filesystem::path path;
    std::atomic<bool> back_buffer_ready{false};

   private:
    std::array<std::vector<uint8_t>, 2> blocks_;
    int read_index_ = 0;

   public:
    Double_Buffer() {
        blocks_[0].resize(BUFFER_SIZE, 0);  //
        blocks_[1].resize(BUFFER_SIZE, 0);
    }

    std::span<uint8_t> GetReadSpan() { return std::span(blocks_[read_index_].data(), BUFFER_SIZE); }

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
    virtual void Close() = 0;
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
    // Virtual Cast idiom
    virtual IAudioProcessor* AsAudio() { return nullptr; }
    std::string id;
    NodeKind kind;
    Node* target;

    // Execution state used by the Session loop
    int processed_frames{0};
    int total_frames{0};
    int in_buffer_procced_frames{0};
};

struct FileOptionsNode : Node {
    double gain{1.0};
    explicit FileOptionsNode(Node* t = nullptr) : Node(t) { kind = NodeKind::FileOptions; }
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
    bool is_first_read = true;
    size_t offset_size = WAV_HEADER_SIZE;
    std::shared_ptr<FileOptionsNode> options;
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

    void Close() override {
        file_handle.close();
        in_buffer_procced_frames = 0;
        processed_frames = 0;
        is_first_read = true;
    }

    void SetOptions(std::shared_ptr<FileOptionsNode> options_node) {
        options = options_node;
        spdlog::info("setted file options with gain {}", options->gain);
    }

    void Open() {
        boost::system::error_code ec;
        file_handle.open(file_path, asio::file_base::read_only, ec);
        if (!ec && file_handle.is_open()) {
            total_frames = file_handle.size() / FRAME_SIZE_BYTES;
            spdlog::info("total frames {}", total_frames);
            return;
        }
        spdlog::error("exception while opening file {}", ec.what());
    }

    // --- IAsyncInitializer Implementation ---
    asio::awaitable<void> InitilizeBuffers() override {
        spdlog::info("initilizing buffer for file input");
        if (!file_handle.is_open()) Open();

        // Initial double-buffer fill
        bf.set_read_index(1);
        co_await RequestRefillAsync();
        bf.back_buffer_ready = true;
        bf.Swap();
        co_await RequestRefillAsync();
        bf.back_buffer_ready = true;
    }

    void ApplyEffects(std::span<uint8_t> frame_buffer) {
        if (options == nullptr) return;

        // Add Gain for now
        auto gain = options->gain;

        auto* samples = reinterpret_cast<int16_t*>(frame_buffer.data());
        for (size_t i = 0; i < SAMPLES_PER_FRAME; i++) {
            int32_t current_sample = samples[i];
            int32_t boosted = static_cast<int32_t>(current_sample * gain);
            samples[i] = static_cast<int16_t>(std::clamp(boosted, -32768, 32786));
        }
    }
    // --- IAudioProcessor Implementation ---
    void ProcessFrame(std::span<uint8_t> frame_buffer) override {
        auto current_span = bf.GetReadSpan();
        size_t buffer_offset = in_buffer_procced_frames * FRAME_SIZE_BYTES;
        if (is_first_read) {
            offset_size = WavUtils::GetAudioDataOffset(current_span);
            buffer_offset += offset_size;
            is_first_read = false;
        }
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
        } else if (processed_frames > total_frames) {
            // if we get called but we already read everything possible with mixer, return 0 buffer;
            std::ranges::fill(frame_buffer, 0);
            return;
        }
        auto start_it = current_span.begin() + buffer_offset;
        auto bruh_span = std::span<uint8_t>(start_it, start_it + FRAME_SIZE_BYTES);
        ApplyEffects(bruh_span);

        std::copy(start_it, start_it + FRAME_SIZE_BYTES, frame_buffer.begin());
        in_buffer_procced_frames++;
        processed_frames++;
    }

    // Helper for internal use
    asio::awaitable<void> RequestRefillAsync() {
        if (!file_handle.is_open()) co_return;

        // Get the back buffer
        auto buffer_span = bf.GetWriteSpan();
        int offset = 0;

        auto [ec, bytes_read] = co_await asio::async_read(file_handle, asio::buffer(buffer_span),
                                                          asio::as_tuple(asio::use_awaitable));

        if (ec) {
            if (ec == asio::error::eof) {
                // EOF is normal for the end of the file.
                // We kept the data in buffer_span[0...bytes_read].
                // We only need to silence the REST of the buffer.
                spdlog::info("[{}] EOF reached. Read {} bytes. Padding remainder.", id, bytes_read);
            } else {
                spdlog::error("[{}] Read error: {}", id, ec.message());
                // On real error, maybe silence everything or just what wasn't read
            }
        }

        if (bytes_read < buffer_span.size()) {
            std::fill(buffer_span.begin() + bytes_read, buffer_span.end(), 0);
        }

        bf.back_buffer_ready = true;
    };
};
// Mixer Node (Needs Audio + Async Init for inputs)
struct MixerNode : Node, IAudioProcessor {
    std::vector<FileInputNode*> inputs;  // Dependency on FileInput
    std::array<int32_t, SAMPLES_PER_FRAME> accumulator_;
    std::array<uint8_t, FRAME_SIZE_BYTES> temp_input_buffer_;
    void SetMaxFrames() {
        int max = 0;
        for (auto* node : inputs) {
            spdlog::info("node for {} has {} frames", node->file_name, node->total_frames);
            max = std::max(max, node->total_frames);
        }
        spdlog::info("max total frames {}", max);
        total_frames = max;
    }

    explicit MixerNode(Node* t = nullptr) : Node(t) {
        spdlog::info("creating mixer");
        kind = NodeKind::Mixer;
    }

    void AddInput(FileInputNode* node) {
        inputs.push_back(node);
        spdlog::info("added input to mixer");
    }

    IAudioProcessor* AsAudio() override { return this; }

    void Close() override {
        for (auto* input : inputs) {
            auto* bruh = input->AsAudio();
            bruh->Close();
        }
        in_buffer_procced_frames = 0;
        processed_frames = 0;
    }
    // Audio Processing
    void ProcessFrame(std::span<uint8_t> frame_buffer) override {
        // 1. reset accumulator
        accumulator_.fill(0);

        bool has_active_inputs = false;

        for (auto* input_node : inputs) {
            if (auto* audio_source = input_node->AsAudio()) {
                has_active_inputs = true;

                // Fetch input
                audio_source->ProcessFrame(temp_input_buffer_);
                const auto* input_samples =
                    std::bit_cast<const int16_t*>(temp_input_buffer_.data());

                // Summing (Mixing)
                for (size_t i = 0; i < SAMPLES_PER_FRAME; ++i) {
                    accumulator_[i] += input_samples[i];
                }
            }
        }

        auto* output_samples = std::bit_cast<int16_t*>(frame_buffer.data());

        if (!has_active_inputs) {
            std::fill(frame_buffer.begin(), frame_buffer.end(), 0);
            return;
        }

        // Apply Soft Clipping
        // Formula: output = 32767 * tanh(input / 32767)
        // This ensures the output NEVER exceeds the valid range, but sounds smooth.

        for (size_t i = 0; i < SAMPLES_PER_FRAME; ++i) {
            int32_t raw_sum = accumulator_[i];

            // Optimization: Only run expensive math if we are clipping
            if (raw_sum > CLIP_LIMIT_POSITIVE || raw_sum < CLIP_LIMIT_NEGATIVE) {
                float compressed = std::tanh(raw_sum / MAX_INT16);
                output_samples[i] = static_cast<int16_t>(compressed * MAX_INT16);
            } else {
                // Safe range: Pass through directly (Linear)
                output_samples[i] = static_cast<int16_t>(raw_sum);
            }
        }

        in_buffer_procced_frames++;
        processed_frames++;
    }
};

// 3. Delay Node (Audio only, no Async IO)
struct DelayNode : Node, IAudioProcessor {
    float delay_ms{0};
    explicit DelayNode(Node* t = nullptr) : Node(t) { kind = NodeKind::Delay; }

    IAudioProcessor* AsAudio() override { return this; }

    void Close() override {
        in_buffer_procced_frames = 0;
        processed_frames = 0;
    }

    void ProcessFrame(std::span<uint8_t> frame_buffer) override {
        std::fill(frame_buffer.begin(), frame_buffer.end(), 0);
        in_buffer_procced_frames++;
        processed_frames++;
    }
};

// 4. Clients Node (Just identity or metadata)
struct ClientsNode : Node {
    // Direct map - no unique_ptr needed
    std::unordered_map<std::string, uint16_t> clients;

    explicit ClientsNode(Node* t = nullptr) : Node(t) { kind = NodeKind::Clients; }

    // Helper to populate
    void AddClient(std::string ip, uint16_t port) { clients.emplace(std::move(ip), port); }
};

// 5. File Options Node (Pure Config, NO Audio methods)

// -------- Graph Container --------
struct Graph {
  // for easy traversal when looking for nodes
    std::vector<std::shared_ptr<Node>> nodes;
    std::unordered_map<std::string, std::shared_ptr<Node>> node_map;
    Node* start_node = nullptr;
};
