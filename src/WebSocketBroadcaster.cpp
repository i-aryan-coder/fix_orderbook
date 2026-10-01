#include "WebSocketBroadcaster.h"

#include <utility>

WebSocketBroadcaster::WebSocketBroadcaster(OutboundBroadcastQueue& queue) : queue_(queue) {}

WebSocketBroadcaster::~WebSocketBroadcaster() {
    stop();
}

void WebSocketBroadcaster::addSink(Sink sink) {
    std::lock_guard<std::mutex> lock(sinksMutex_);
    sinks_.push_back(std::move(sink));
}

void WebSocketBroadcaster::start() {
    if (running_.exchange(true)) {
        return;
    }
    worker_ = std::thread(&WebSocketBroadcaster::loop, this);
}

void WebSocketBroadcaster::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    queue_.stop();
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool WebSocketBroadcaster::isRunning() const {
    return running_.load();
}

size_t WebSocketBroadcaster::deliveredCount() const {
    return deliveredCount_.load();
}

void WebSocketBroadcaster::loop() {
    while (running_.load()) {
        OutboundMessage message;
        if (!queue_.waitPop(message)) {
            break;
        }

        std::vector<Sink> sinksCopy;
        {
            std::lock_guard<std::mutex> lock(sinksMutex_);
            sinksCopy = sinks_;
        }
        for (const auto& sink : sinksCopy) {
            sink(message);
        }
        ++deliveredCount_;
    }
}
