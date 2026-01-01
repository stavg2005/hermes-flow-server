#include "Nodes.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <bit>
#include <cmath>

constexpr int RETRY_DELAY_MS = 50;

#include "WavUtils.hpp"

static constexpr int max_16_bytes = 32767;
static constexpr int min_16_bytes = -32767;
// =========================================================
//  Double_Buffer
// =========================================================

Double_Buffer::Double_Buffer() {
    blocks_[0].resize(BUFFER_SIZE, 0);
    blocks_[1].resize(BUFFER_SIZE, 0);
}

std::span<uint8_t> Double_Buffer::GetReadSpan() {
    return std::span(blocks_[read_index_].data(), BUFFER_SIZE);
}

std::span<uint8_t> Double_Buffer::GetWriteSpan() {
    return std::span(blocks_[read_index_ ^ 1]);
}

void Double_Buffer::set_read_index(int value) {
    if (value == 0 || value == 1) read_index_ = value;
}

void Double_Buffer::Swap() {
    read_index_ ^= 1;
    back_buffer_ready = false;
}

// =========================================================
//  Node Base & Config
// =========================================================

Node::Node(Node* t) : target(t) {}

IAudioProcessor* Node::AsAudio() {
    return nullptr;
}

FileOptionsNode::FileOptionsNode(Node* t) : Node(t) {
    kind = NodeKind::FileOptions;
}

// =========================================================
//  FileInputNode
// =========================================================

FileInputNode::FileInputNode(asio::io_context& io, std::string name, std::string path)
    : Node(nullptr),
      file_name(std::move(name)),
      file_path(std::move(path)),
      refill_threshold_frames(BUFFER_SIZE / FRAME_SIZE_BYTES / 2),
      file_handle(io) {
    kind = NodeKind::FileInput;
    bf.path = file_path;
    offset_size = WAV_HEADER_SIZE;
}

IAudioProcessor* FileInputNode::AsAudio() {
    return this;
}

void FileInputNode::SetOptions(std::shared_ptr<FileOptionsNode> options_node) {
    options = std::move(options_node);
    if (options) {
        spdlog::info("[{}] Set gain option: {}", file_name, options->gain);
    }
}

void FileInputNode::Open() {
    constexpr int max_retries = 3;
    int attempt = 0;
    boost::system::error_code ec;

    while (attempt < max_retries) {
        file_handle.open(file_path, asio::file_base::read_only, ec);

        if (!ec && file_handle.is_open()) {
            total_frames = static_cast<int>(file_handle.size() / FRAME_SIZE_BYTES);
            spdlog::info("[{}] Opened file. Total frames: {}", file_name, total_frames);
            return;
        }

        spdlog::warn("[{}] Attempt {}: Failed to open file {}: {}", file_name, attempt + 1,
                     file_path, ec.message());
        attempt++;

        std::this_thread::sleep_for(std::chrono::milliseconds(RETRY_DELAY_MS));
    }

    spdlog::error("[{}] Failed to open file {} after {} attempts.", file_name, file_path,
                  max_retries);

    total_frames = 0;  // mark node as inactive
}

void FileInputNode::Close() {
    boost::system::error_code ec;
    file_handle.close(ec);
    in_buffer_processed_frames = 0;
    processed_frames = 0;
    is_first_read = true;
}

asio::awaitable<void> FileInputNode::InitializeBuffers() {
    spdlog::info("[{}] Initializing buffers...", file_name);
    if (!file_handle.is_open()) Open();

    // Initial fill
    bf.set_read_index(1);
    co_await RequestRefillAsync();
    bf.back_buffer_ready = true;

    bf.Swap();

    co_await RequestRefillAsync();
    bf.back_buffer_ready = true;
}

void FileInputNode::ProcessFrame(std::span<uint8_t> frame_buffer) {
    auto current_span = bf.GetReadSpan();
    size_t buffer_offset = in_buffer_processed_frames * FRAME_SIZE_BYTES;

    // calculate the data offset in the wav file because  some files may not be the standart 44
    // bytes,for example really short files
    if (is_first_read) {
        offset_size = WavUtils::GetAudioDataOffset(current_span);
        buffer_offset += offset_size;
        is_first_read = false;
    }

    // Check if we need to swap buffers on the next frame
    if (buffer_offset + FRAME_SIZE_BYTES > current_span.size()) {
        if (!bf.back_buffer_ready) {
            std::fill(frame_buffer.begin(), frame_buffer.end(), 0);  // Underrun
            return;
        }

        bf.Swap();
        in_buffer_processed_frames = 0;
        current_span = bf.GetReadSpan();
        buffer_offset = 0;

        // co spawning detached to Trigger background refill
        asio::co_spawn(
            file_handle.get_executor(),
            [self = shared_from_this()]() { return self->RequestRefillAsync(); }, asio::detached);

    } else if (processed_frames > total_frames) {
        std::ranges::fill(frame_buffer, 0);
        return;
    }

    // Copy to output
    auto start_it = current_span.begin() + buffer_offset;
    auto data_span = std::span<uint8_t>(start_it, start_it + FRAME_SIZE_BYTES);

    ApplyEffects(data_span);
    std::copy(data_span.begin(), data_span.end(), frame_buffer.begin());

    in_buffer_processed_frames++;
    processed_frames++;
}

void FileInputNode::ApplyEffects(std::span<uint8_t> frame_buffer) {
    if (options == nullptr || options->gain == 1.0) return;

    auto gain = options->gain;
    auto* samples = reinterpret_cast<int16_t*>(frame_buffer.data());

    for (size_t i = 0; i < SAMPLES_PER_FRAME; i++) {
        int32_t current_sample = samples[i];
        auto boosted = static_cast<int32_t>(current_sample * gain);
        samples[i] = static_cast<int16_t>(std::clamp(boosted, min_16_bytes, max_16_bytes));
    }
}

asio::awaitable<void> FileInputNode::RequestRefillAsync() {
    if (!file_handle.is_open()) co_return;

    auto buffer_span = bf.GetWriteSpan();
    constexpr int max_retries = 3;
    int attempt = 0;

    while (attempt < max_retries) {
        auto [ec, bytes_read] = co_await asio::async_read(file_handle, asio::buffer(buffer_span),
                                                          asio::as_tuple(asio::use_awaitable));

        if (!ec || ec == asio::error::eof) {
            if (bytes_read < buffer_span.size()) {
                std::fill(buffer_span.begin() + bytes_read, buffer_span.end(), 0);
            }
            bf.back_buffer_ready = true;
            co_return;
        } else {
            spdlog::warn("[{}] Refill read attempt {} failed: {}", file_name, attempt + 1,
                         ec.message());
            attempt++;

            co_await asio::steady_timer(file_handle.get_executor(),
                                        std::chrono::milliseconds(RETRY_DELAY_MS))
                .async_wait(asio::use_awaitable);
        }
    }

    // After max retries, fail gracefully
    spdlog::error("[{}] Refill failed after {} attempts. Filling buffer with zeros.", file_name,
                  max_retries);
    std::fill(buffer_span.begin(), buffer_span.end(), 0);
    bf.back_buffer_ready = true;
}

// =========================================================
//  MixerNode
// =========================================================

MixerNode::MixerNode(Node* t) : Node(t) {
    kind = NodeKind::Mixer;
}

IAudioProcessor* MixerNode::AsAudio() {
    return this;
}

void MixerNode::SetMaxFrames() {
    int max = 0;
    for (auto* node : inputs) {
        max = std::max(max, node->total_frames);
    }
    total_frames = max;
    spdlog::info("Mixer total frames set to: {}", max);
}

void MixerNode::AddInput(FileInputNode* node) {
    inputs.push_back(node);
}

void MixerNode::Close() {
    for (auto* input : inputs) {
        if (auto* audio = input->AsAudio()) {
            audio->Close();
        }
    }
    in_buffer_processed_frames = 0;
    processed_frames = 0;
}

void MixerNode::ProcessFrame(std::span<uint8_t> frame_buffer) {
    accumulator_.fill(0);
    bool has_active_inputs = false;

    for (auto* input_node : inputs) {
        if (auto* audio_source = input_node->AsAudio()) {
            has_active_inputs = true;

            audio_source->ProcessFrame(temp_input_buffer_);

            const auto* input_samples = reinterpret_cast<const int16_t*>(
                temp_input_buffer_.data());  // NOLINT  reinterpret_cast dosent copy unlike bit_cast
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

    // Soft limiter (tanh) to prevent clipping.
    // TODO maybe use a lookup table of tanh instead of calulating tough on 8khz its negligible
    for (size_t i = 0; i < SAMPLES_PER_FRAME; ++i) {
        int32_t raw_sum = accumulator_[i];
        if (raw_sum > CLIP_LIMIT_POSITIVE || raw_sum < CLIP_LIMIT_NEGATIVE) {
            float compressed = std::tanh(static_cast<float>(raw_sum) / MAX_INT16);
            output_samples[i] = static_cast<int16_t>(compressed * MAX_INT16);
        } else {
            output_samples[i] = static_cast<int16_t>(raw_sum);
        }
    }

    in_buffer_processed_frames++;
    processed_frames++;
}

// =========================================================
//  DelayNode & ClientsNode
// =========================================================

DelayNode::DelayNode(Node* t) : Node(t) {
    kind = NodeKind::Delay;
}
IAudioProcessor* DelayNode::AsAudio() {
    return this;
}

void DelayNode::Close() {
    in_buffer_processed_frames = 0;
    processed_frames = 0;
}

void DelayNode::ProcessFrame(std::span<uint8_t> frame_buffer) {
    std::fill(frame_buffer.begin(), frame_buffer.end(), 0);
    in_buffer_processed_frames++;
    processed_frames++;
}

ClientsNode::ClientsNode(Node* t) : Node(t) {
    kind = NodeKind::Clients;
}

void ClientsNode::AddClient(std::string ip, uint16_t port) {
    clients.emplace(std::move(ip), port);
}
