#include "MatchingEngine.h"
#include <iostream>

MatchingEngine::MatchingEngine() {
    currentSnapshot_ = std::make_shared<const OrderbookSnapshot>(
        0, std::chrono::system_clock::now(), orderbook_.getorderinfo());
}

MatchingEngine::~MatchingEngine() {
    stop(true);
}

void MatchingEngine::start() {
    if (isRunning_.exchange(true)) {
        return; // Already running
    }
    workerThread_ = std::thread(&MatchingEngine::workerLoop, this);
}

void MatchingEngine::stop(bool drainQueue) {
    if (!isRunning_.exchange(false)) {
        return; // Already stopped
    }
    // drainQueue is always honoured: wait_and_pop() drains remaining items
    // before returning false when the queue is stopped and empty.
    (void)drainQueue;
    queue_.stop();
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
    sequenceCv_.notify_all();
}

bool MatchingEngine::isRunning() const {
    return isRunning_.load();
}

uint64_t MatchingEngine::submitNewOrder(orderptr order) {
    uint64_t seq = nextSequenceNumber_.fetch_add(1);
    queue_.push(OrderEvent::makeNewOrder(seq, std::move(order)));
    return seq;
}

uint64_t MatchingEngine::submitCancelOrder(int orderId) {
    uint64_t seq = nextSequenceNumber_.fetch_add(1);
    queue_.push(OrderEvent::makeCancelOrder(seq, orderId));
    return seq;
}

uint64_t MatchingEngine::submitModifyOrder(const OrderModify& omod) {
    uint64_t seq = nextSequenceNumber_.fetch_add(1);
    queue_.push(OrderEvent::makeModifyOrder(seq, omod));
    return seq;
}

std::future<OrderEventResult> MatchingEngine::submitNewOrderSync(orderptr order) {
    auto prom = std::make_shared<std::promise<OrderEventResult>>();
    auto fut = prom->get_future();
    uint64_t seq = nextSequenceNumber_.fetch_add(1);
    queue_.push(OrderEvent::makeNewOrder(seq, std::move(order), prom));
    return fut;
}

std::future<OrderEventResult> MatchingEngine::submitCancelOrderSync(int orderId) {
    auto prom = std::make_shared<std::promise<OrderEventResult>>();
    auto fut = prom->get_future();
    uint64_t seq = nextSequenceNumber_.fetch_add(1);
    queue_.push(OrderEvent::makeCancelOrder(seq, orderId, prom));
    return fut;
}

std::future<OrderEventResult> MatchingEngine::submitModifyOrderSync(const OrderModify& omod) {
    auto prom = std::make_shared<std::promise<OrderEventResult>>();
    auto fut = prom->get_future();
    uint64_t seq = nextSequenceNumber_.fetch_add(1);
    queue_.push(OrderEvent::makeModifyOrder(seq, omod, prom));
    return fut;
}

bool MatchingEngine::submit(OrderEvent event) {
    if (event.sequenceNumber == 0) {
        event.sequenceNumber = nextSequenceNumber_.fetch_add(1);
    }
    return queue_.push(std::move(event));
}

void MatchingEngine::addTradeListener(TradeListener listener) {
    std::lock_guard<std::mutex> lock(listenersMutex_);
    tradeListeners_.push_back(std::move(listener));
}

void MatchingEngine::addEventListener(EventListener listener) {
    std::lock_guard<std::mutex> lock(listenersMutex_);
    eventListeners_.push_back(std::move(listener));
}

std::shared_ptr<const OrderbookSnapshot> MatchingEngine::getSnapshot() const {
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    return currentSnapshot_;
}

uint64_t MatchingEngine::getLastProcessedSequence() const {
    return lastProcessedSequence_.load();
}

size_t MatchingEngine::getQueueSize() const {
    return queue_.size();
}

void MatchingEngine::waitForSequence(uint64_t seq) {
    std::unique_lock<std::mutex> lock(sequenceMutex_);
    sequenceCv_.wait(lock, [this, seq]() {
        return lastProcessedSequence_.load() >= seq || !isRunning_.load();
    });
}

void MatchingEngine::publishSnapshot(uint64_t seq) {
    auto newSnapshot = std::make_shared<const OrderbookSnapshot>(
        seq, std::chrono::system_clock::now(), orderbook_.getorderinfo());

    std::lock_guard<std::mutex> lock(snapshotMutex_);
    currentSnapshot_ = std::move(newSnapshot);
}

void MatchingEngine::workerLoop() {
    while (true) {
        OrderEvent event;
        if (!queue_.wait_and_pop(event)) {
            // Queue stopped and drained
            break;
        }

        if (event.type == EventType::Shutdown) {
            break;
        }

        processEvent(event);
    }
}

void MatchingEngine::processEvent(OrderEvent& event) {
    OrderEventResult result;
    result.sequenceNumber = event.sequenceNumber;
    result.eventType = event.type;
    result.orderId = event.orderId;
    result.success = true;

    try {
        switch (event.type) {
            case EventType::NewOrder: {
                if (!event.order) {
                    throw std::invalid_argument("Cannot process null order");
                }
                result.executedTrades = orderbook_.addorder(event.order);
                break;
            }
            case EventType::CancelOrder: {
                orderbook_.cancelorder(event.orderId);
                break;
            }
            case EventType::ModifyOrder: {
                result.executedTrades = orderbook_.Matchorder(event.modifyReq);
                break;
            }
            default:
                break;
        }
    } catch (const std::exception& e) {
        result.success = false;
        result.errorMessage = e.what();
    } catch (...) {
        result.success = false;
        result.errorMessage = "Unknown error occurred during order processing";
    }

    // Publish read-only snapshot for external consumers
    publishSnapshot(event.sequenceNumber);

    // Notify trade listeners
    if (!result.executedTrades.empty()) {
        std::vector<TradeListener> listenersCopy;
        {
            std::lock_guard<std::mutex> lock(listenersMutex_);
            listenersCopy = tradeListeners_;
        }
        // Use 'executedTrade' to avoid shadowing the 'trade' typedef in Order.h
        for (const auto& executedTrade : result.executedTrades) {
            for (const auto& listener : listenersCopy) {
                listener(executedTrade);
            }
        }
    }

    // Notify event result listeners
    {
        std::vector<EventListener> listenersCopy;
        {
            std::lock_guard<std::mutex> lock(listenersMutex_);
            listenersCopy = eventListeners_;
        }
        for (const auto& listener : listenersCopy) {
            listener(result);
        }
    }

    // Fulfill promise for synchronous callers
    if (event.promiseResult) {
        try {
            event.promiseResult->set_value(result);
        } catch (...) {
            // In case promise was already fulfilled or broken
        }
    }

    // Update sequence number and notify any waiters
    {
        std::lock_guard<std::mutex> lock(sequenceMutex_);
        lastProcessedSequence_.store(event.sequenceNumber);
    }
    sequenceCv_.notify_all();
}
