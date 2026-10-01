#include "OutboundBroadcastQueue.h"

#include <utility>

OutboundBroadcastQueue::OutboundBroadcastQueue(size_t capacity)
    : capacity_(capacity == 0 ? 1 : capacity) {}

bool OutboundBroadcastQueue::tryPush(OutboundMessage message) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_) {
            return false;
        }
        if (queue_.size() >= capacity_) {
            queue_.pop_front();
            ++droppedCount_;
        }
        queue_.push_back(std::move(message));
    }
    cv_.notify_one();
    return true;
}

std::optional<OutboundMessage> OutboundBroadcastQueue::tryPop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) {
        return std::nullopt;
    }
    OutboundMessage message = std::move(queue_.front());
    queue_.pop_front();
    return message;
}

bool OutboundBroadcastQueue::waitPop(OutboundMessage& message) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this]() {
        return stopped_ || !queue_.empty();
    });
    if (queue_.empty()) {
        return false;
    }
    message = std::move(queue_.front());
    queue_.pop_front();
    return true;
}

void OutboundBroadcastQueue::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
    }
    cv_.notify_all();
}

size_t OutboundBroadcastQueue::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

size_t OutboundBroadcastQueue::droppedCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return droppedCount_;
}
