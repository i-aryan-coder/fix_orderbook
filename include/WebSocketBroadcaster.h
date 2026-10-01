#ifndef WEBSOCKETBROADCASTER_H
#define WEBSOCKETBROADCASTER_H

#include "OutboundBroadcastQueue.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class WebSocketBroadcaster {
public:
    using Sink = std::function<void(const OutboundMessage&)>;

    explicit WebSocketBroadcaster(OutboundBroadcastQueue& queue);
    ~WebSocketBroadcaster();

    WebSocketBroadcaster(const WebSocketBroadcaster&) = delete;
    WebSocketBroadcaster& operator=(const WebSocketBroadcaster&) = delete;

    void addSink(Sink sink);
    void start();
    void stop();
    bool isRunning() const;
    size_t deliveredCount() const;

private:
    void loop();

    OutboundBroadcastQueue& queue_;
    std::vector<Sink> sinks_;
    mutable std::mutex sinksMutex_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<size_t> deliveredCount_{0};
};

#endif // WEBSOCKETBROADCASTER_H
