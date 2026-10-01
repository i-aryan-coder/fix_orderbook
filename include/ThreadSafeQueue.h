#ifndef THREADSAFEQUEUE_H
#define THREADSAFEQUEUE_H

#include <queue>
#include <mutex>
#include <condition_variable>
#include <utility>

/**
 * @brief Thread-safe multi-producer, single-consumer (MPSC) queue.
 *
 * Implements blocking wait with condition variable (no busy-waiting)
 * and graceful shutdown / queue draining.
 */
template <typename T>
class ThreadSafeQueue {
public:
    ThreadSafeQueue() = default;
    ~ThreadSafeQueue() {
        stop();
    }

    // Non-copyable, non-movable for synchronization safety
    ThreadSafeQueue(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;

    /**
     * @brief Push an item into the queue. Wakes the waiting consumer.
     * @return true if pushed, false if queue is stopped.
     */
    bool push(T item) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopped_) {
                return false;
            }
            queue_.push(std::move(item));
        }
        cv_.notify_one();
        return true;
    }

    /**
     * @brief Blocks until an item is available or queue is stopped and drained.
     * @param value Out-parameter to receive the popped item.
     * @return true if an item was successfully popped, false if queue is stopped and empty.
     */
    bool wait_and_pop(T& value) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this]() {
            return !queue_.empty() || stopped_;
        });

        if (queue_.empty()) {
            return false; // Stopped and all items consumed
        }

        value = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    /**
     * @brief Non-blocking try-pop.
     */
    bool try_pop(T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return false;
        }
        value = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    /**
     * @brief Signal queue to stop accepting new items and wake waiting threads.
     */
    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopped_ = true;
        }
        cv_.notify_all();
    }

    bool is_stopped() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return stopped_;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool stopped_{false};
};

#endif // THREADSAFEQUEUE_H
