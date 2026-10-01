#ifndef OUTBOUNDBROADCASTQUEUE_H
#define OUTBOUNDBROADCASTQUEUE_H

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <string>

struct OutboundMessage {
    enum class Type {
        Orderbook,
        Trade,
        OrderUpdate
    };

    Type type{Type::Orderbook};
    std::string payload;
};

class OutboundBroadcastQueue {
public:
    explicit OutboundBroadcastQueue(size_t capacity = 1024);

    bool tryPush(OutboundMessage message);
    std::optional<OutboundMessage> tryPop();
    bool waitPop(OutboundMessage& message);
    void stop();
    size_t size() const;
    size_t droppedCount() const;

private:
    size_t capacity_;
    std::deque<OutboundMessage> queue_;
    bool stopped_{false};
    size_t droppedCount_{0};
    mutable std::mutex mutex_;
    std::condition_variable cv_;
};

#endif // OUTBOUNDBROADCASTQUEUE_H
