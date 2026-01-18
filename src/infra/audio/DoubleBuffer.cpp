#include "DoubleBuffer.hpp"

#include "Config.hpp"
using namespace hermes::config;
namespace hermes::infra {

DoubleBuffer::DoubleBuffer() {
  blocks_[0].resize(BUFFER_SIZE, 0);
  blocks_[1].resize(BUFFER_SIZE, 0);
}

std::span<uint8_t> DoubleBuffer::GetReadSpan() {
  return std::span(blocks_[read_index_].data(), BUFFER_SIZE);
}

std::span<uint8_t> DoubleBuffer::GetWriteSpan() {
  return std::span(blocks_[read_index_ ^ 1]);
}

void DoubleBuffer::SetReadIndex(int value) {
  if (value == 0 || value == 1) read_index_ = value;
}

void DoubleBuffer::Swap() {
  read_index_ ^= 1;
  back_buffer_ready_ = false;
}
}  // namespace hermes::infra
