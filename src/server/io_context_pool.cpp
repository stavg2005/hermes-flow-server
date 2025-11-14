#include "io_context_pool.hpp"

#include <iostream>
#include <stdexcept>

#include "spdlog/spdlog.h"

class io_context_pool_exception : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};
namespace net = boost::asio;

io_context_pool::io_context_pool(std::size_t pool_size)
    // --- ADDED ---
    // Initialize next_io_context_ (was missing)
    : next_io_context_(0) {
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
    // We can call stop() again, it's safe.
    stop();
  } catch (const std::exception &e) {
    std::cerr << "Exception during io_context_pool shutdown: " << e.what()
              << std::endl;
  } catch (...) {
    std::cerr << "Unknown exception during io_context_pool shutdown"
              << std::endl;
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
  // Get the next io_context
  spdlog::debug("Fetching IO contect from pool");
  spdlog::debug("go io context");
  // Increment and wrap around for the next call
  std::size_t i = next_io_context_.fetch_add(1, std::memory_order_relaxed);

  return *io_contexts_[i % io_contexts_.size()];
}
