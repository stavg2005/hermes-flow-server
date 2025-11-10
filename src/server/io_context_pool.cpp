#include "io_context_pool.hpp" // 1. Include the header

#include <iostream>  // For std::cerr in the run() lambda
#include <stdexcept> // For custom exception base

// Dedicated exception for io_context_pool errors
class io_context_pool_exception : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};
namespace net = boost::asio;

io_context_pool::io_context_pool(std::size_t pool_size) {

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
        std::cerr << "io_context_pool thread boost::system::system_error: "
                  << e.what() << std::endl;
      }
    });
  }
}

void io_context_pool::stop() {
  // 1. Destroy all work guards.
  work_guards_.clear();

  // 2. Explicitly stop all io_contexts.
  for (const auto &ioc : io_contexts_) {
    if (ioc && !ioc->stopped()) {
      ioc->stop();
    }
  }

  // 3. Join all threads. (std::jthread would also do this on destruction)
  for (auto &t : threads_) {
    if (t.joinable()) {
      t.join();
    }
  }

  // 4. Clear the thread vector
  threads_.clear();
}

net::io_context &io_context_pool::get_io_context() {
  // Get the next io_context
  auto &ioc = *io_contexts_[next_io_context_];

  // Increment and wrap around for the next call
  next_io_context_ = (next_io_context_ + 1) % io_contexts_.size();

  return ioc;
}