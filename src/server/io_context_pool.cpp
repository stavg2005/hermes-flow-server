#include "io_context_pool.hpp"

#include <iostream>
#include <stdexcept>

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
                  << e.what() << std::endl;
      }
    });
  }


  // The run() function now blocks the main thread by joining
  // all worker threads. It will only unblock when the
  // threads exit, which happens after stop() is called.
  for (auto &t : threads_) {
    if (t.joinable()) {
      t.join();
    }
  }


  // Clear the thread vector once they've all been joined.
  threads_.clear();
}

void io_context_pool::stop() {
  // Destroy all work guards.
  // This allows the io_context::run() calls in the threads
  // to return once they finish their current work.
  work_guards_.clear();

  // Explicitly stop all io_contexts.
  // This will interrupt any blocking operations.
  for (const auto &ioc : io_contexts_) {
    if (ioc && !ioc->stopped()) {
      ioc->stop();
    }
  }

}

net::io_context &io_context_pool::get_io_context() {
  // Get the next io_context
  auto &ioc = *io_contexts_[next_io_context_];

  // Increment and wrap around for the next call
  next_io_context_ = (next_io_context_ + 1) % io_contexts_.size();

  return ioc;
}
