#ifndef MATCHINGENGINE_H
#define MATCHINGENGINE_H

#include "Orderbook.h"
#include "OrderEvent.h"
#include "ThreadSafeQueue.h"
#include "OrderbookSnapshot.h"

#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <functional>
#include <memory>
#include <future>

/**
 * @brief Thread-safe Matching Engine wrapper providing single-ownership of the canonical Orderbook.
 *
 * Concurrency Architecture (Phase 2):
 * - External producers (FIX, REST, WebSocket, Test threads) submit order events to a thread-safe queue.
 * - Exactly ONE dedicated Matching Engine worker thread consumes events from the queue sequentially.
 * - The canonical Orderbook is NEVER touched directly by external threads.
 * - Resulting trades are published to listeners (ready for Phase 3 FIX ExecutionReports).
 * - Read-only immutable snapshots are published under a lightweight pointer swap, so slow
 *   consumers (REST/WebSocket) never block the matching thread.
 */
class MatchingEngine {
public:
    using TradeListener = std::function<void(const Trade&)>;
    using EventListener = std::function<void(const OrderEventResult&)>;

    MatchingEngine();
    ~MatchingEngine();

    // Disallow copy/assignment
    MatchingEngine(const MatchingEngine&) = delete;
    MatchingEngine& operator=(const MatchingEngine&) = delete;

    // Lifecycle
    void start();
    void stop(bool drainQueue = true);
    bool isRunning() const;

    // Asynchronous Order Ingestion API (Fire-and-forget for high throughput)
    uint64_t submitNewOrder(orderptr order);
    uint64_t submitCancelOrder(int orderId);
    uint64_t submitModifyOrder(const OrderModify& omod);

    // Synchronous Order Ingestion API (Returns future with execution result)
    std::future<OrderEventResult> submitNewOrderSync(orderptr order);
    std::future<OrderEventResult> submitCancelOrderSync(int orderId);
    std::future<OrderEventResult> submitModifyOrderSync(const OrderModify& omod);

    // Generic Event submission
    bool submit(OrderEvent event);

    // Event & Trade Listeners (Phase 3 & 4 preparation)
    void addTradeListener(TradeListener listener);
    void addEventListener(EventListener listener);

    // Read-only Snapshot API
    std::shared_ptr<const OrderbookSnapshot> getSnapshot() const;
    uint64_t getLastProcessedSequence() const;
    size_t getQueueSize() const;

    // Drain / Barrier helper for synchronization (waits until all queued events are processed)
    void waitForSequence(uint64_t seq);

private:
    void workerLoop();
    void processEvent(OrderEvent& event);
    void publishSnapshot(uint64_t seq);

    // The canonical Orderbook - single ownership by this matching engine instance,
    // accessed EXCLUSIVELY by workerLoop() / processEvent().
    Orderbook orderbook_;

    // Ingestion queue
    ThreadSafeQueue<OrderEvent> queue_;

    // Dedicated worker thread
    std::thread workerThread_;
    std::atomic<bool> isRunning_{false};
    // NOTE: Drain-on-stop is guaranteed by ThreadSafeQueue::wait_and_pop() semantics:
    // it drains all remaining items before returning false when stopped+empty.
    // No separate drainOnStop_ flag is needed.

    // Monotonic sequence numbering
    std::atomic<uint64_t> nextSequenceNumber_{1};
    std::atomic<uint64_t> lastProcessedSequence_{0};

    // Snapshot publishing (protected by minimal mutex for atomic pointer swap)
    std::shared_ptr<const OrderbookSnapshot> currentSnapshot_;
    mutable std::mutex snapshotMutex_;
    std::condition_variable sequenceCv_;
    mutable std::mutex sequenceMutex_;

    // Listeners
    std::vector<TradeListener> tradeListeners_;
    std::vector<EventListener> eventListeners_;
    std::mutex listenersMutex_;
};

#endif // MATCHINGENGINE_H
