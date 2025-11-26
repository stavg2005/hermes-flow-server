#include "io_context_pool.hpp"

#include <iostream>
#include <stdexcept>

#include "spdlog/spdlog.h"

class io_context_pool_exception : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};
namespace net = boost::asio;

io_context_pool::io_context_pool(std::size_t pool_size){
  if (pool_size == 0) {
    throw io_context_pool_exception("io_context_pool size must be > 0");
  }
  // Create the io_contexts and their work guards
  for (std::size_t i = 0; i < pool_size; ++i) {
    auto ioc = std::make_shared<net::io_context>();
    io_contexts_.push_back(ioc);
    work_guards_.push_back(net::make_work_guard(*ioc));
  }
}

io_context_pool::~io_context_pool() {
  try {
    stop();
      //jthreads join on their own
  } catch (const std::exception &e) {
    // ...
  }
}


void io_context_pool::run() {
  // Prevent double-run
  if (!threads_.empty()) {
    return;
  }

  // Create one thread per io_context.
  for (const auto &ioc : io_contexts_) {
    threads_.emplace_back([ioc]() {
      try {
        ioc->run();
      } catch (const boost::system::system_error &e) {
        // This can happen on unclean shutdown, but we'll log it.
        std::cerr << "io_context_pool thread boost::system::system_error: "
                  << e.what() << "\n";
      }
    });
  }
}

void io_context_pool::stop() {
  work_guards_.clear();  // Let ioc->run() return

  for (const auto &ioc : io_contexts_) {
    if (ioc && !ioc->stopped()) {
      ioc->stop();  // Stop any blocking ops
    }
  }
}

net::io_context &io_context_pool::get_io_context() {
  spdlog::debug("Fetching IO context from pool");
  std::size_t idx = next_io_context_.fetch_add(1, std::memory_order_relaxed) %
                    io_contexts_.size();

  try {
    auto &ptr = io_contexts_[idx];
    spdlog::debug("Shared_ptr use_count = {}", ptr.use_count());
    if (!ptr) {
      spdlog::critical("io_context pointer is null at index {}", idx);
      throw std::runtime_error("null io_context");
    }
    spdlog::debug("Returning io_context at index {}", idx);
    return *ptr;
  } catch (const std::exception &e) {
    spdlog::critical("Exception accessing io_context: {}", e.what());
    throw;
  } catch (...) {
    spdlog::critical("Unknown exception accessing io_context at index {}", idx);
    throw;
  }
}
