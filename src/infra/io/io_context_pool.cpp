#include "io_context_pool.hpp"
#include <stdexcept>

#include "spdlog/spdlog.h"

io_context_pool::io_context_pool(std::size_t pool_size) {
  if (pool_size == 0) {
    throw std::runtime_error("io_context_pool size must be > 0");
  }

  for (std::size_t i = 0; i < pool_size; ++i) {
    auto ioc = std::make_shared<asio::io_context>();
    io_contexts_.push_back(ioc);
    work_guards_.emplace_back(asio::make_work_guard(*ioc));
  }
}

io_context_pool::~io_context_pool() { stop(); }

void io_context_pool::run() {

  if (!threads_.empty()) {
    return;
  }

  spdlog::info("Starting I/O pool with {} threads.", io_contexts_.size());


  for (const auto& ioc : io_contexts_) {
    threads_.emplace_back([ioc]() {
      try {
        ioc->run();
      } catch (const std::exception& e) {
        spdlog::critical("io_context thread exception: {}", e.what());
      }
    });
  }
}

void io_context_pool::stop() {
  work_guards_.clear();

  for (const auto& ioc : io_contexts_) {
    if (ioc && !ioc->stopped()) {
      ioc->stop();
    }
  }
}

asio::io_context& io_context_pool::get_io_context() {
  // Thread safe Round-robin selection
  std::size_t idx = next_io_context_.fetch_add(1, std::memory_order_relaxed) %
                    io_contexts_.size();

  auto& ptr = io_contexts_[idx];
  if (!ptr) {
    spdlog::critical("io_context is null at index {}", idx);
    throw std::runtime_error("null io_context in pool");
  }

  return *ptr;
}
