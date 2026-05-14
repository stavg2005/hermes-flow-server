#include "DoubleBuffer.hpp"

#include "core/config/Config.hpp"


namespace hermes::infra {

DoubleBuffer::DoubleBuffer() {
  blocks_[0].resize(hermes::config::BUFFER_SIZE, 0);
  blocks_[1].resize(hermes::config::BUFFER_SIZE, 0);
}

std::span<uint8_t> DoubleBuffer::get_read_span() {
  // Use memory_order_relaxed because synchronization is handled by
  // back_buffer_ready_
  return blocks_[read_index_.load(std::memory_order_relaxed)];
}

std::span<uint8_t> DoubleBuffer::get_write_span() {
  int current_read = read_index_.load(std::memory_order_relaxed);
  return blocks_[1 - current_read];
}

void DoubleBuffer::set_read_index(int value) {
  read_index_.store(value, std::memory_order_relaxed);
}

void DoubleBuffer::swap() {
  int current_read = read_index_.load(std::memory_order_relaxed);
  read_index_.store(1 - current_read, std::memory_order_relaxed);
}

void DoubleBuffer::reset() {
  set_read_index(0);
  back_buffer_ready_.store(false, std::memory_order_release);
  std::fill(blocks_[0].begin(), blocks_[0].end(), 0);
  std::fill(blocks_[1].begin(), blocks_[1].end(), 0);
}

}  // namespace hermes::infra
