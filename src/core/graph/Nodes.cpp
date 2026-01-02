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
//  DoubleBuffer
// =========================================================

DoubleBuffer::DoubleBuffer() {
    blocks_[0].resize(BUFFER_SIZE, 0);
    blocks_[1].resize(BUFFER_SIZE, 0);
}

std::span<uint8_t> DoubleBuffer::get_read_span() {
    return std::span(blocks_[read_index_].data(), BUFFER_SIZE);
}

std::span<uint8_t> DoubleBuffer::get_write_span() {
    return std::span(blocks_[read_index_ ^ 1]);
}

void DoubleBuffer::set_read_index(int value) {
    if (value == 0 || value == 1) read_index_ = value;
}

void DoubleBuffer::swap() {
    read_index_ ^= 1;
    back_buffer_ready_ = false;
}

// =========================================================
//  Node Base & Config
// =========================================================

Node::Node(Node* t) : target_(t) {}

IAudioProcessor* Node::as_audio() {
    return nullptr;
}

FileOptionsNode::FileOptionsNode(Node* t) : Node(t) {
    kind_ = NodeKind::FileOptions;
}

// =========================================================
//  FileInputNode
// =========================================================

FileInputNode::FileInputNode(boost::asio::io_context& io, std::string name, std::string path)
    : Node(nullptr),
      file_name_(std::move(name)),
      file_path_(std::move(path)),
      refill_threshold_frames_(BUFFER_SIZE / FRAME_SIZE_BYTES / 2),
      file_handle_(io) {
    kind_ = NodeKind::FileInput;
    bf_.path_ = file_path_;
    offset_size_ = WAV_HEADER_SIZE;
}

IAudioProcessor* FileInputNode::as_audio() {
    return this;
}

void FileInputNode::set_options(std::shared_ptr<FileOptionsNode> options_node) {
    options_ = std::move(options_node);
    if (options_) {
        spdlog::info("[{}] Set gain option: {}", file_name_, options_->gain);
    }
}

void FileInputNode::open() {
    constexpr int max_retries = 3;
    int attempt = 0;
    boost::system::error_code ec;

    while (attempt < max_retries) {
        file_handle_.open(file_path_, boost::asio::file_base::read_only, ec);

        if (!ec && file_handle_.is_open()) {
            total_frames_ = static_cast<int>(file_handle_.size() / FRAME_SIZE_BYTES);
            spdlog::info("[{}] Opened file. Total frames: {}", file_name_, total_frames_);
            return;
        }

        spdlog::warn("[{}] Attempt {}: Failed to open file {}: {}", file_name_, attempt + 1,
                     file_path_, ec.message());
        attempt++;

        std::this_thread::sleep_for(std::chrono::milliseconds(RETRY_DELAY_MS));
    }

    spdlog::error("[{}] Failed to open file {} after {} attempts.", file_name_, file_path_,
                  max_retries);

    total_frames_ = 0;  // mark node as inactive
}

void FileInputNode::close() {
    boost::system::error_code ec;
    file_handle_.close(ec);
    in_buffer_processed_frames_ = 0;
    processed_frames_ = 0;
    is_first_read_ = true;
}

boost::asio::awaitable<void> FileInputNode::initialize_buffers() {
    spdlog::info("[{}] Initializing buffers...", file_name_);
    if (!file_handle_.is_open()) open();

    // Initial fill
    bf_.set_read_index(1);
    co_await request_refill_async();
    bf_.back_buffer_ready_ = true;

    bf_.swap();

    co_await request_refill_async();
    bf_.back_buffer_ready_ = true;
}

void FileInputNode::process_frame(std::span<uint8_t> frame_buffer) {
    auto current_span = bf_.get_read_span();
    size_t buffer_offset = in_buffer_processed_frames_ * FRAME_SIZE_BYTES;

    if (is_first_read_) {
        offset_size_ = WavUtils::GetAudioDataOffset(current_span);
        buffer_offset += offset_size_;
        is_first_read_ = false;
    }

    // Check if we need to swap buffers on the next frame
    if (buffer_offset + FRAME_SIZE_BYTES > current_span.size()) {
        if (!bf_.back_buffer_ready_) {
            std::fill(frame_buffer.begin(), frame_buffer.end(), 0);  // Underrun
            return;
        }

        bf_.swap();
        in_buffer_processed_frames_ = 0;
        current_span = bf_.get_read_span();
        buffer_offset = 0;

        boost::asio::co_spawn(
            file_handle_.get_executor(),
            [self = shared_from_this()]() { return self->request_refill_async(); },
            boost::asio::detached);

    } else if (processed_frames_ > total_frames_) {
        std::ranges::fill(frame_buffer, 0);
        return;
    }

    // Copy to output
    auto start_it = current_span.begin() + buffer_offset;
    auto data_span = std::span<uint8_t>(start_it, start_it + FRAME_SIZE_BYTES);

    apply_effects(data_span);
    std::copy(data_span.begin(), data_span.end(), frame_buffer.begin());

    in_buffer_processed_frames_++;
    processed_frames_++;
}

void FileInputNode::apply_effects(std::span<uint8_t> frame_buffer) {
    if (options_ == nullptr || options_->gain == 1.0) return;

    auto gain = options_->gain;
    auto* samples = reinterpret_cast<int16_t*>(frame_buffer.data());

    for (size_t i = 0; i < SAMPLES_PER_FRAME; i++) {
        int32_t current_sample = samples[i];
        auto boosted = static_cast<int32_t>(current_sample * gain);
        samples[i] = static_cast<int16_t>(std::clamp(boosted, min_16_bytes, max_16_bytes));
    }
}

boost::asio::awaitable<void> FileInputNode::request_refill_async() {
    if (!file_handle_.is_open()) co_return;

    auto buffer_span = bf_.get_write_span();
    constexpr int max_retries = 3;
    int attempt = 0;

    while (attempt < max_retries) {
        auto [ec, bytes_read] =
            co_await boost::asio::async_read(file_handle_, boost::asio::buffer(buffer_span),
                                             boost::asio::as_tuple(boost::asio::use_awaitable));

        if (!ec || ec == boost::asio::error::eof) {
            if (bytes_read < buffer_span.size()) {
                std::fill(buffer_span.begin() + bytes_read, buffer_span.end(), 0);
            }
            bf_.back_buffer_ready_ = true;
            co_return;
        } else {
            spdlog::warn("[{}] Refill read attempt {} failed: {}", file_name_, attempt + 1,
                         ec.message());
            attempt++;

            boost::asio::steady_timer timer(file_handle_.get_executor());
            timer.expires_after(std::chrono::milliseconds(RETRY_DELAY_MS));
            co_await timer.async_wait(boost::asio::use_awaitable);
        }
    }

    spdlog::error("[{}] Refill failed after {} attempts. Filling buffer with zeros.", file_name_,
                  max_retries);
    std::fill(buffer_span.begin(), buffer_span.end(), 0);
    bf_.back_buffer_ready_ = true;
}

// =========================================================
//  MixerNode
// =========================================================

MixerNode::MixerNode(Node* t) : Node(t) {
    kind_ = NodeKind::Mixer;
}

IAudioProcessor* MixerNode::as_audio() {
    return this;
}

void MixerNode::set_max_frames() {
    int max = 0;
    for (auto* node : inputs_) {
        max = std::max(max, node->total_frames_);
    }
    total_frames_ = max;
    spdlog::info("Mixer total frames set to: {}", max);
}

void MixerNode::add_input(FileInputNode* node) {
    inputs_.push_back(node);
}

void MixerNode::close() {
    for (auto* input : inputs_) {
        if (auto* audio = input->as_audio()) {
            audio->close();
        }
    }
    in_buffer_processed_frames_ = 0;
    processed_frames_ = 0;
}

void MixerNode::process_frame(std::span<uint8_t> frame_buffer) {
    accumulator_.fill(0);
    bool has_active_inputs = false;

    for (auto* input_node : inputs_) {
        if (auto* audio_source = input_node->as_audio()) {
            has_active_inputs = true;

            audio_source->process_frame(temp_input_buffer_);

            const auto* input_samples = reinterpret_cast<const int16_t*>(temp_input_buffer_.data());
            for (size_t i = 0; i < SAMPLES_PER_FRAME; ++i) {
                accumulator_[i] += input_samples[i];
            }
        }
    }

    auto* output_samples = reinterpret_cast<int16_t*>(frame_buffer.data());

    if (!has_active_inputs) {
        std::fill(frame_buffer.begin(), frame_buffer.end(), 0);
        return;
    }

    for (size_t i = 0; i < SAMPLES_PER_FRAME; ++i) {
        int32_t raw_sum = accumulator_[i];
        if (raw_sum > CLIP_LIMIT_POSITIVE || raw_sum < CLIP_LIMIT_NEGATIVE) {
            float compressed = std::tanh(static_cast<float>(raw_sum) / MAX_INT16);
            output_samples[i] = static_cast<int16_t>(compressed * MAX_INT16);
        } else {
            output_samples[i] = static_cast<int16_t>(raw_sum);
        }
    }

    in_buffer_processed_frames_++;
    processed_frames_++;
}

// =========================================================
//  DelayNode & ClientsNode
// =========================================================

DelayNode::DelayNode(Node* t) : Node(t) {
    kind_ = NodeKind::Delay;
}

IAudioProcessor* DelayNode::as_audio() {
    return this;
}

void DelayNode::close() {
    in_buffer_processed_frames_ = 0;
    processed_frames_ = 0;
}

void DelayNode::process_frame(std::span<uint8_t> frame_buffer) {
    std::fill(frame_buffer.begin(), frame_buffer.end(), 0);
    in_buffer_processed_frames_++;
    processed_frames_++;
}

ClientsNode::ClientsNode(Node* t) : Node(t) {
    kind_ = NodeKind::Clients;
}

void ClientsNode::add_client(std::string ip, uint16_t port) {
    clients.emplace(std::move(ip), port);
}
