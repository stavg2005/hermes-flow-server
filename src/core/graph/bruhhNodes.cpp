#include "Nodes.hpp"
/*
namespace asio = boost::asio;

// --- FileInput Implementation ---

FileInput::FileInput(asio::io_context& io, std::string name, std::string path)
    : file_name(std::move(name)), file_path(std::move(path)), file_handle(io) {
    bf.path = file_path;
}

void FileInput::Open(int& total_frames_out) {
    boost::system::error_code ec;
    file_handle.open(file_path, asio::file_base::read_only, ec);
    if (ec) {
        spdlog::error("Exception while opening file {}: {}", file_path, ec.message());
        total_frames_out = 0;  // Error case
        return;
    }

    if (file_handle.is_open()) {
        uint64_t file_size = file_handle.size();
        total_frames_out = static_cast<int>(file_size / FRAME_SIZE_BYTES);
        spdlog::info("[{}] Opened file. Size: {} bytes, Frames: {}", file_name, file_size,
                     total_frames_out);
    }
}

void FileInput::Close(Node& parent) {
    parent.processed_frames = 0;

    is_first_read = true;
    in_buffer_procced_frames = 0;

    // 3. Reset the double buffer state
    bf.back_buffer_ready = false;
    bf.set_read_index(0);
}
}
// NOTE: We pass 'processed_frames' and 'total_frames' from the wrapper Node because structs don't
// store them anymore
void FileInput::ProcessFrame(std::span<uint8_t> frame_buffer, Node& parent) {
    auto current_span = bf.GetReadSpan();
    size_t buffer_offset = in_buffer_procced_frames * FRAME_SIZE_BYTES;

    if (is_first_read) {
        offset_size = WavUtils::GetAudioDataOffset(current_span);
        buffer_offset += offset_size;
        is_first_read = false;
    }

    // Check if we need to swap buffers
    if (buffer_offset + FRAME_SIZE_BYTES > current_span.size()) {
        if (!bf.back_buffer_ready) {
            std::fill(frame_buffer.begin(), frame_buffer.end(), 0);  // Underrun
            return;
        }

        bf.Swap();
        in_buffer_procced_frames = 0;
        current_span = bf.GetReadSpan();
        buffer_offset = 0;

        // Trigger refill
        asio::co_spawn(
            file_handle.get_executor(),
            [self = parent.shared_from_this()]() {
                // We must retrieve the FileInput from the Node wrapper safely
                if (auto* fi = std::get_if<FileInput>(&self->data)) {
                    return fi->RequestRefillAsync(self);
                }
                return asio::awaitable<void>{};  // Should never happen
            },
            asio::detached);
    } else if (parent.processed_frames > parent.total_frames) {
        std::ranges::fill(frame_buffer, 0);
        return;
    }

    auto start_it = current_span.begin() + buffer_offset;
    // Safety check for vector bounds could go here
    auto data_span = std::span<uint8_t>(start_it, start_it + FRAME_SIZE_BYTES);

    // Copy to output
    std::copy(data_span.begin(), data_span.end(), frame_buffer.begin());

    // Apply Gain IN PLACE
    ApplyEffects(frame_buffer);

    in_buffer_procced_frames++;
    parent.processed_frames++;
}

void FileInput::ApplyEffects(std::span<uint8_t> frame_buffer) {
    if (gain == 1.0) return;  // Optimization

    auto* samples = reinterpret_cast<int16_t*>(frame_buffer.data());
    for (size_t i = 0; i < SAMPLES_PER_FRAME; i++) {
        int32_t current = samples[i];
        int32_t boosted = static_cast<int32_t>(current * gain);
        samples[i] = static_cast<int16_t>(std::clamp(boosted, -32768, 32767));
    }
}

boost::asio::awaitable<void> FileInput::RequestRefillAsync(std::shared_ptr<Node> self_node) {
    if (!file_handle.is_open()) co_return;

    auto buffer_span = bf.GetWriteSpan();
    auto [ec, bytes_read] = co_await asio::async_read(file_handle, asio::buffer(buffer_span),
                                                      asio::as_tuple(asio::use_awaitable));

    if (ec && ec != asio::error::eof) {
        spdlog::error("Read error: {}", ec.message());
    }

    if (bytes_read < buffer_span.size()) {
        std::fill(buffer_span.begin() + bytes_read, buffer_span.end(), 0);
    }
    bf.back_buffer_ready = true;
}

boost::asio::awaitable<void> FileInput::InitilizeBuffers(int& total_frames_out) {
    // Pass the reference down to Open
    if (!file_handle.is_open()) Open(total_frames_out);

    bf.set_read_index(1);

    // Initial Buffer Fills
    // (We need to handle the read logic here. Since we don't have 'self' shared_ptr
    // easily here, we can manually do the first reads like this):

    auto buffer_span = bf.GetWriteSpan();
    auto [ec1, bytes1] = co_await asio::async_read(file_handle, asio::buffer(buffer_span),
                                                   asio::as_tuple(asio::use_awaitable));
    bf.back_buffer_ready = true;

    bf.Swap();

    buffer_span = bf.GetWriteSpan();
    auto [ec2, bytes2] = co_await asio::async_read(file_handle, asio::buffer(buffer_span),
                                                   asio::as_tuple(asio::use_awaitable));
    bf.back_buffer_ready = true;
}

// --- Mixer Implementation ---

void Mixer::SetMaxFrames(int& total_frames_out) {
    int max = 0;
    for (auto* node : inputs) {
        max = std::max(max, node->total_frames);
    }
    total_frames_out = max;
    spdlog::info("Mixer configured with max frames: {}", max);
}

void Mixer::ProcessFrame(std::span<uint8_t> frame_buffer, Node& parent) {
    accumulator_.fill(0);
    bool has_active_inputs = false;

    for (auto* input_node : inputs) {
        // MODERN REPLACEMENT FOR AsAudio():
        // We check if the input node holds a FileInput

            has_active_inputs = true;

            // Process the input into our temp buffer
            // We pass the wrapper's state variables by reference
            fileInput->ProcessFrame(temp_input_buffer_, *input_node);

            // Mix
            const auto* input_samples = std::bit_cast<const int16_t*>(temp_input_buffer_.data());
            for (size_t i = 0; i < SAMPLES_PER_FRAME; ++i) {
                accumulator_[i] += input_samples[i];

        }
    }

    auto* output_samples = std::bit_cast<int16_t*>(frame_buffer.data());

    if (!has_active_inputs) {
        std::fill(frame_buffer.begin(), frame_buffer.end(), 0);
        return;
    }

    // Soft Clipping (Tanh)
    for (size_t i = 0; i < SAMPLES_PER_FRAME; ++i) {
        int32_t raw = accumulator_[i];
        // Simple hard clip for speed, or use your Tanh logic
        output_samples[i] = static_cast<int16_t>(std::clamp(raw, -32768, 32767));
    }

    parent.processed_frames++;
}

void Mixer::Close(Node& parent) {
for (auto& input : inputs) {
        // Direct call, no recursion through visit needed
        input->Close();
        input.wrapper->processed_frames = 0;
    }

    accumulator_.fill(0);
    parent.processed_frames = 0;
}

void Delay::ProcessFrame(std::span<uint8_t> frame_buffer, Node& parent) {
    std::fill(frame_buffer.begin(), frame_buffer.end(), 0);
    parent.processed_frames++;
}

void Clients::AddClient(std::string ip, uint16_t port) {
    clients.emplace(std::move(ip), port);
}
*/
