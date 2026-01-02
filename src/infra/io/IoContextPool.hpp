
//to avoid collision with already exisiting class from boost bruh
#ifndef IO_CONTEXT_POOL_HPP
#define IO_CONTEXT_POOL_HPP

#include <boost/asio.hpp>
#include <cstddef>
#include <memory>
#include <thread>
#include <vector>

#include "Types.hpp"

/**
 * @brief Manages a pool of `io_context` instances, each pinning a thread.
 * Thread pool with one io_context per thread.
 * Assigns contexts via round-robin.
 */
class IoContextPool {
   public:
    explicit IoContextPool(std::size_t pool_size);

    // Destructor. Stops and joins all threads.
    ~IoContextPool();

    // Disable copying
    IoContextPool(const IoContextPool&) = delete;
    IoContextPool& operator=(const IoContextPool&) = delete;

    void run();

    // Stops all io_context objects and joins all threads.
    void stop();

    //@brief Get an io_context from the pool in a round-robin fashion.
    asio::io_context& get_io_context();

   private:
    std::vector<std::shared_ptr<asio::io_context>> io_contexts_;

    using work_guard_type = asio::executor_work_guard<asio::io_context::executor_type>;
    std::vector<work_guard_type> work_guards_;

    std::vector<std::jthread> threads_;

    std::atomic<std::size_t> next_io_context_{0};
};
#endif
