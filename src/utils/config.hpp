#pragma once
#include <chrono>


static constexpr size_t SAMPLES_PER_FRAME = 160;
static constexpr size_t BYTES_PER_SAMPLE = 2;
static constexpr size_t FRAME_SIZE_BYTES = SAMPLES_PER_FRAME * BYTES_PER_SAMPLE;  // 16-bit PCM
static constexpr size_t WAV_HEADER_SIZE = 44;
static constexpr int MS = 20;
static constexpr size_t PAYLOAD_TYPE = 8;
static constexpr size_t BUFFER_SIZE=1024*128;
static constexpr size_t RTP_HEADER_SIZE =12;
