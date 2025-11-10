#ifndef IO_CONTEXT_POOL_HPP
#define IO_CONTEXT_POOL_HPP

#include <boost/asio.hpp>
#include <cstddef> // For std::size_t
#include <memory>
#include <thread> // For std::jthread
#include <vector>

namespace net = boost::asio;

/**
 * @class io_context_pool
 * @brief A fixed-size pool of io_context objects, each running in its own
 * thread.
 */
class io_context_pool {
public:
  /**
   * @brief Constructs the io_context pool.
   * @param pool_size The number of io_context objects (and threads) to create.
   */
  explicit io_context_pool(std::size_t pool_size);

  /**
   * @brief Destructor. Stops and joins all threads.
   */
  ~io_context_pool();

  // Disable copying
  io_context_pool(const io_context_pool &) = delete;
  io_context_pool &operator=(const io_context_pool &) = delete;

  /**
   * @brief Starts all threads in the pool.
   */
  void run();

  /**
   * @brief Stops all io_context objects and joins all threads.
   */
  void stop();

  /**
   * @brief Get an io_context from the pool in a round-robin fashion.
   * @return A reference to an io_context.
   */
  net::io_context &get_io_context();

private:
  // --- Data Members ---
  std::vector<std::shared_ptr<net::io_context>> io_contexts_;

  using work_guard_type =
      net::executor_work_guard<net::io_context::executor_type>;
  std::vector<work_guard_type> work_guards_;

  std::vector<std::jthread> threads_;

  std::size_t next_io_context_{0};
};

#endif // IO_CONTEXT_POOL_HPP