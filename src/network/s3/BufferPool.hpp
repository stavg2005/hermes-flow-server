#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <stack>
#include <vector>


/**
 * @brief Fixed-size memory pool. Returns buffers via custom shared_ptr deleter.
 */
class BufferPool {
   public:
    using BufferPtr = std::shared_ptr<std::vector<uint8_t>>;

    static BufferPool& Instance(size_t initial_count = 8, size_t buffer_size = 512 * 1024) {
        static BufferPool instance(initial_count, buffer_size);
        return instance;
    }

    /**
     * @brief Acquire a buffer from the pool.
     * Returns a shared_ptr that automatically returns the buffer on destruction.
     * @param size Requested buffer size (ignored if pre-allocated buffers are large enough)
     */
    BufferPtr Acquire(size_t size = 512 * 1024) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::unique_ptr<std::vector<uint8_t>> raw_ptr;

        if (!pool_.empty()) {
            raw_ptr = std::move(pool_.top());
            pool_.pop();
        } else {
            // Rare fallback allocation if all pre-allocated buffers are in use
            raw_ptr = std::make_unique<std::vector<uint8_t>>();
            raw_ptr->reserve(size);
            raw_ptr->resize(size);
        }

        if (raw_ptr->size() != size) {
            raw_ptr->resize(size);  // cheap if reserve already done
        }

        return BufferPtr(raw_ptr.release(), [this](std::vector<uint8_t>* p) { Release(p); });
    }

   private:
    BufferPool(size_t initial_count, size_t buffer_size) {
        for (size_t i = 0; i < initial_count; ++i) {
            auto vec = std::make_unique<std::vector<uint8_t>>();
            vec->reserve(buffer_size);
            vec->resize(buffer_size);
            pool_.push(std::move(vec));
        }
    }

    ~BufferPool() {
        while (!pool_.empty()) {
            pool_.pop();
            pool_.pop();
        }
    }

    void Release(std::vector<uint8_t>* p) {
        if (!p) return;
        std::lock_guard<std::mutex> lock(mutex_);
        pool_.push(std::unique_ptr<std::vector<uint8_t>>(p));
    }

    std::stack<std::unique_ptr<std::vector<uint8_t>>> pool_;
    std::mutex mutex_;


};
