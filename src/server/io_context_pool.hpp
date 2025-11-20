#ifndef IO_CONTEXT_POOL_HPP
#define IO_CONTEXT_POOL_HPP

#include <boost/asio.hpp>
#include <cstddef>  // For std::size_t
#include <memory>
#include <thread>  // For std::jthread
#include <vector>

namespace net = boost::asio;

class io_context_pool {
 public:
  explicit io_context_pool(std::size_t pool_size);

  // Destructor. Stops and joins all threads.
  ~io_context_pool();

  // Disable copying
  io_context_pool(const io_context_pool &) = delete;
  io_context_pool &operator=(const io_context_pool &) = delete;

  void run();

  // Stops all io_context objects and joins all threads.

  void stop();

  //@brief Get an io_context from the pool in a round-robin fashion.
  net::io_context &get_io_context();

 private:
  std::vector<std::shared_ptr<net::io_context>> io_contexts_;

  using work_guard_type =
      net::executor_work_guard<net::io_context::executor_type>;
  std::vector<work_guard_type> work_guards_;

  std::vector<std::jthread> threads_;

  std::atomic<std::size_t> next_io_context_{0};
};

#endif
