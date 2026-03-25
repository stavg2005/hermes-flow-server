#include "DoubleBuffer.hpp"

#include "Config.hpp"
using namespace hermes::config;
namespace hermes::infra {

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

void DoubleBuffer::reset() {
  std::fill(blocks_[0].begin(), blocks_[0].end(), 0);
  std::fill(blocks_[1].begin(), blocks_[1].end(), 0);
}
}  // namespace hermes::infra
