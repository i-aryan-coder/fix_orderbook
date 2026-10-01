#include "MatchingEngine.h"
#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <set>

/* -----------------------------------------------------
   Lightweight Test Harness
   ----------------------------------------------------- */

static int g_concTotalTests = 0;
static int g_concPassedTests = 0;
static int g_concFailedTests = 0;

#define TEST_CASE(name) \
    void name(); \
    struct Register_##name { \
        Register_##name() { \
            g_concTotalTests++; \
            try { \
                std::cout << "[RUN ] " << #name << std::endl; \
                name(); \
                g_concPassedTests++; \
                std::cout << "[PASS] " << #name << "\n" << std::endl; \
            } catch (const std::exception& e) { \
                g_concFailedTests++; \
                std::cerr << "[FAIL] " << #name << " - Exception: " << e.what() << "\n" << std::endl; \
            } catch (...) { \
                g_concFailedTests++; \
                std::cerr << "[FAIL] " << #name << " - Unknown exception\n" << std::endl; \
            } \
        } \
    } instance_##name; \
    void name()

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            throw std::runtime_error(std::string("Assertion failed: ") + #cond + " at line " + std::to_string(__LINE__)); \
        } \
    } while(0)

#define ASSERT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            throw std::runtime_error(std::string("Assertion failed: ") + #a + " == " + #b + \
                " (" + std::to_string(a) + " != " + std::to_string(b) + ") at line " + std::to_string(__LINE__)); \
        } \
    } while(0)

/* -----------------------------------------------------
   CONCURRENCY & QUEUE TESTS
   ----------------------------------------------------- */

// 1. Sequential Correctness: MatchingEngine vs Phase 1 Orderbook
TEST_CASE(TestC01_SequentialCorrectnessMatchingEngine) {
    Orderbook directBook;
    MatchingEngine engine;
    engine.start();

    // Sequence: Sell 100x10, Sell 101x10, Market Buy 15, FAK Buy 101x10
    auto dTrades1 = directBook.addorder(std::make_shared<Order>(OrderType::Limit, 1, Side::Sell, 100, 10));
    auto dTrades2 = directBook.addorder(std::make_shared<Order>(OrderType::Limit, 2, Side::Sell, 101, 10));
    auto dTrades3 = directBook.addorder(std::make_shared<Order>(3, Side::Buy, 15));
    auto dTrades4 = directBook.addorder(std::make_shared<Order>(OrderType::FillAndKill, 4, Side::Buy, 101, 10));

    auto fut1 = engine.submitNewOrderSync(std::make_shared<Order>(OrderType::Limit, 1, Side::Sell, 100, 10));
    auto fut2 = engine.submitNewOrderSync(std::make_shared<Order>(OrderType::Limit, 2, Side::Sell, 101, 10));
    auto fut3 = engine.submitNewOrderSync(std::make_shared<Order>(3, Side::Buy, 15));
    auto fut4 = engine.submitNewOrderSync(std::make_shared<Order>(OrderType::FillAndKill, 4, Side::Buy, 101, 10));

    auto res1 = fut1.get();
    auto res2 = fut2.get();
    auto res3 = fut3.get();
    auto res4 = fut4.get();

    ASSERT_EQ(res1.executedTrades.size(), dTrades1.size());
    ASSERT_EQ(res2.executedTrades.size(), dTrades2.size());
    ASSERT_EQ(res3.executedTrades.size(), dTrades3.size());
    ASSERT_EQ(res4.executedTrades.size(), dTrades4.size());

    // Compare resulting snapshots
    auto directAgg = directBook.getorderinfo();
    auto engineSnap = engine.getSnapshot();

    ASSERT_EQ(directAgg.getbids().size(), engineSnap->getbids().size());
    ASSERT_EQ(directAgg.getasks().size(), engineSnap->getasks().size());
    if (!directAgg.getasks().empty()) {
        ASSERT_EQ(directAgg.getasks()[0].price, engineSnap->getasks()[0].price);
        ASSERT_EQ(directAgg.getasks()[0].quantity, engineSnap->getasks()[0].quantity);
    }

    engine.stop(true);
}

// 2. Queue Ordering Preservation
TEST_CASE(TestC02_QueueOrderingPreserved) {
    ThreadSafeQueue<int> q;
    for (int i = 0; i < 100; ++i) {
        q.push(i);
    }
    ASSERT_EQ(q.size(), 100u);

    for (int i = 0; i < 100; ++i) {
        int val = -1;
        bool ok = q.wait_and_pop(val);
        ASSERT_TRUE(ok);
        ASSERT_EQ(val, i);
    }
    ASSERT_TRUE(q.empty());
}

// 3. Queue Condition Variable: Empty Wait and Producer Wakeup
TEST_CASE(TestC03_QueueEmptyWaitAndWakeup) {
    ThreadSafeQueue<int> q;
    std::atomic<bool> consumerReady{false};
    std::atomic<int> receivedValue{-1};

    std::thread consumer([&]() {
        consumerReady = true;
        int val = -1;
        if (q.wait_and_pop(val)) {
            receivedValue = val;
        }
    });

    while (!consumerReady) {
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Producer pushes item, waking up consumer
    q.push(42);
    consumer.join();

    ASSERT_EQ(receivedValue.load(), 42);
}

// 4. Queue Shutdown and Draining
TEST_CASE(TestC04_QueueShutdownAndDrain) {
    ThreadSafeQueue<int> q;
    q.push(1);
    q.push(2);
    q.push(3);

    q.stop();
    ASSERT_TRUE(q.is_stopped());

    // Should not accept new items after stop
    bool pushedAfterStop = q.push(4);
    ASSERT_TRUE(!pushedAfterStop);

    // Should still drain existing items
    int val = 0;
    ASSERT_TRUE(q.wait_and_pop(val));
    ASSERT_EQ(val, 1);
    ASSERT_TRUE(q.wait_and_pop(val));
    ASSERT_EQ(val, 2);
    ASSERT_TRUE(q.wait_and_pop(val));
    ASSERT_EQ(val, 3);

    // When empty and stopped, wait_and_pop returns false
    ASSERT_TRUE(!q.wait_and_pop(val));
}

// 5. Multi-Producer Ingestion: 10 producers x 100 orders (1000 orders total)
TEST_CASE(TestC05_MultiProducerConcurrentIngestion) {
    MatchingEngine engine;
    engine.start();

    const int kNumProducers = 10;
    const int kOrdersPerProducer = 100;
    std::vector<std::thread> producers;
    producers.reserve(kNumProducers);

    for (int p = 0; p < kNumProducers; ++p) {
        producers.emplace_back([&engine, p]() {
            for (int i = 0; i < kOrdersPerProducer; ++i) {
                int orderId = p * 1000 + i + 1; // Unique order ID
                int price = 100 + (orderId % 50);
                engine.submitNewOrder(std::make_shared<Order>(OrderType::Limit, orderId, Side::Sell, price, 5));
            }
        });
    }

    for (auto& t : producers) {
        t.join();
    }

    // Wait for all 1000 orders to be processed
    engine.waitForSequence(kNumProducers * kOrdersPerProducer);

    ASSERT_EQ(engine.getLastProcessedSequence(), static_cast<uint64_t>(kNumProducers * kOrdersPerProducer));
    auto snap = engine.getSnapshot();
    ASSERT_TRUE(snap != nullptr);
    ASSERT_EQ(snap->getSequenceNumber(), static_cast<uint64_t>(kNumProducers * kOrdersPerProducer));

    engine.stop(true);
}

// 6. Concurrent Producers with Crossing Orders and Trade Generation
TEST_CASE(TestC06_ConcurrentCrossingOrders) {
    MatchingEngine engine;
    std::atomic<int> totalTradesExecuted{0};
    std::atomic<int> totalVolumeTraded{0};

    engine.addTradeListener([&](const Trade& t) {
        totalTradesExecuted++;
        totalVolumeTraded += t.getQuantity();
    });

    engine.start();

    // Producer 1: Sells 50 orders of 10 @ 100
    std::thread seller([&engine]() {
        for (int i = 1; i <= 50; ++i) {
            engine.submitNewOrder(std::make_shared<Order>(OrderType::Limit, i, Side::Sell, 100, 10));
        }
    });

    // Producer 2: Buys 50 orders of 10 @ 100
    std::thread buyer([&engine]() {
        for (int i = 51; i <= 100; ++i) {
            engine.submitNewOrder(std::make_shared<Order>(OrderType::Limit, i, Side::Buy, 100, 10));
        }
    });

    seller.join();
    buyer.join();

    engine.waitForSequence(100);
    engine.stop(true);

    // Total sell volume = 500, total buy volume = 500. Exactly 500 must have traded!
    ASSERT_EQ(totalVolumeTraded.load(), 500);
    ASSERT_EQ(totalTradesExecuted.load(), 50);

    auto snap = engine.getSnapshot();
    ASSERT_TRUE(snap->getbids().empty());
    ASSERT_TRUE(snap->getasks().empty());
}

// 7. Cancellation via Concurrent Engine
TEST_CASE(TestC07_CancelThroughConcurrentEngine) {
    MatchingEngine engine;
    engine.start();

    auto fut1 = engine.submitNewOrderSync(std::make_shared<Order>(OrderType::Limit, 101, Side::Buy, 99, 10));
    auto res1 = fut1.get();
    ASSERT_TRUE(res1.success);

    auto snap1 = engine.getSnapshot();
    ASSERT_EQ(snap1->getbids().size(), 1u);
    ASSERT_EQ(snap1->getbids()[0].quantity, 10);

    auto fut2 = engine.submitCancelOrderSync(101);
    auto res2 = fut2.get();
    ASSERT_TRUE(res2.success);

    auto snap2 = engine.getSnapshot();
    ASSERT_TRUE(snap2->getbids().empty());

    engine.stop(true);
}

// 8. Modification via Concurrent Engine
TEST_CASE(TestC08_ModifyThroughConcurrentEngine) {
    MatchingEngine engine;
    engine.start();

    engine.submitNewOrderSync(std::make_shared<Order>(OrderType::Limit, 1, Side::Buy, 100, 10)).get();
    engine.submitNewOrderSync(std::make_shared<Order>(OrderType::Limit, 2, Side::Buy, 100, 10)).get();

    // Reduce Order 1 quantity to 4 (preserves priority)
    auto modFut = engine.submitModifyOrderSync(OrderModify(1, Side::Buy, 100, 4));
    auto modRes = modFut.get();
    ASSERT_TRUE(modRes.success);

    // Sell 6: Order 1 should fill 4, Order 2 fills 2
    auto sellFut = engine.submitNewOrderSync(std::make_shared<Order>(OrderType::Limit, 3, Side::Sell, 100, 6));
    auto sellRes = sellFut.get();
    ASSERT_EQ(sellRes.executedTrades.size(), 2u);
    ASSERT_EQ(sellRes.executedTrades[0].getBuyOrderId(), 1);
    ASSERT_EQ(sellRes.executedTrades[0].getQuantity(), 4);
    ASSERT_EQ(sellRes.executedTrades[1].getBuyOrderId(), 2);
    ASSERT_EQ(sellRes.executedTrades[1].getQuantity(), 2);

    engine.stop(true);
}

// 9. Trade Event Listener Broadcast
TEST_CASE(TestC09_TradeEventListenerBroadcast) {
    MatchingEngine engine;
    std::atomic<int> listener1Calls{0};
    std::atomic<int> listener2Calls{0};

    engine.addTradeListener([&](const Trade&) { listener1Calls++; });
    engine.addTradeListener([&](const Trade&) { listener2Calls++; });

    engine.start();

    engine.submitNewOrder(std::make_shared<Order>(OrderType::Limit, 1, Side::Sell, 100, 10));
    engine.submitNewOrder(std::make_shared<Order>(OrderType::Limit, 2, Side::Buy, 100, 10));

    engine.waitForSequence(2);
    engine.stop(true);

    ASSERT_EQ(listener1Calls.load(), 1);
    ASSERT_EQ(listener2Calls.load(), 1);
}

// 10. Event Error Reporting on Invalid Orders
TEST_CASE(TestC10_EventErrorReporting) {
    MatchingEngine engine;
    std::atomic<int> errorCount{0};

    engine.addEventListener([&](const OrderEventResult& res) {
        if (!res.success) {
            errorCount++;
        }
    });

    engine.start();

    // Submit invalid order: non-existent order cancel
    auto cancelFut = engine.submitCancelOrderSync(9999);
    auto cancelRes = cancelFut.get();
    ASSERT_TRUE(!cancelRes.success);
    ASSERT_TRUE(!cancelRes.errorMessage.empty());

    // Submit invalid modify: non-existent order modify
    auto modFut = engine.submitModifyOrderSync(OrderModify(9999, Side::Buy, 100, 10));
    auto modRes = modFut.get();
    ASSERT_TRUE(!modRes.success);

    ASSERT_EQ(errorCount.load(), 2);
    engine.stop(true);
}

// 11. Read-Only Snapshot Isolation
TEST_CASE(TestC11_ReadOnlySnapshotIsolation) {
    MatchingEngine engine;
    engine.start();

    engine.submitNewOrderSync(std::make_shared<Order>(OrderType::Limit, 1, Side::Buy, 100, 10)).get();
    auto snapshot1 = engine.getSnapshot();
    ASSERT_EQ(snapshot1->getbids().size(), 1u);
    ASSERT_EQ(snapshot1->getbids()[0].quantity, 10);

    // Submit another order
    engine.submitNewOrderSync(std::make_shared<Order>(OrderType::Limit, 2, Side::Buy, 100, 5)).get();
    auto snapshot2 = engine.getSnapshot();

    // snapshot1 is immutable and unaffected by subsequent matching
    ASSERT_EQ(snapshot1->getbids()[0].quantity, 10);
    ASSERT_EQ(snapshot2->getbids()[0].quantity, 15);

    engine.stop(true);
}

// 12. Slow Consumer Non-Blocking
TEST_CASE(TestC12_SlowConsumerNonBlocking) {
    MatchingEngine engine;
    engine.start();

    engine.submitNewOrderSync(std::make_shared<Order>(OrderType::Limit, 1, Side::Sell, 100, 10)).get();

    // Slow consumer thread grabs snapshot and sleeps for 50ms
    std::atomic<bool> slowConsumerFinished{false};
    std::thread slowConsumer([&]() {
        auto snap = engine.getSnapshot();
        ASSERT_EQ(snap->getasks()[0].quantity, 10);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        slowConsumerFinished = true;
    });

    // While slow consumer is sleeping, producer submits 20 orders
    // The matching engine must NOT be blocked by the slow consumer!
    auto startTime = std::chrono::steady_clock::now();
    for (int i = 2; i <= 20; ++i) {
        engine.submitNewOrder(std::make_shared<Order>(OrderType::Limit, i, Side::Sell, 100 + i, 10));
    }
    engine.waitForSequence(20);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime).count();

    // Matching thread processed all 20 orders without waiting 50ms for the slow consumer!
    ASSERT_EQ(engine.getLastProcessedSequence(), 20u);

    slowConsumer.join();
    ASSERT_TRUE(slowConsumerFinished.load());
    (void)elapsed;

    engine.stop(true);
}

// 13. Single-Threaded Mutex Invariant: Only Matching Worker Mutates Orderbook
TEST_CASE(TestC13_SingleThreadMutatorVerification) {
    MatchingEngine engine;
    std::set<std::thread::id> mutatorThreadIds;
    std::mutex mutatorMutex;

    engine.addEventListener([&](const OrderEventResult&) {
        std::lock_guard<std::mutex> lock(mutatorMutex);
        mutatorThreadIds.insert(std::this_thread::get_id());
    });

    engine.start();

    // Multiple producers from different threads
    std::thread t1([&engine]() {
        engine.submitNewOrder(std::make_shared<Order>(OrderType::Limit, 1, Side::Buy, 100, 5));
    });
    std::thread t2([&engine]() {
        engine.submitNewOrder(std::make_shared<Order>(OrderType::Limit, 2, Side::Buy, 100, 5));
    });
    t1.join();
    t2.join();

    engine.waitForSequence(2);
    engine.stop(true);

    // All order processing callbacks MUST have been executed by exactly ONE thread: the worker thread!
    ASSERT_EQ(mutatorThreadIds.size(), 1u);
    ASSERT_TRUE(*mutatorThreadIds.begin() != std::this_thread::get_id());
}

// 14. Graceful Startup and Shutdown
TEST_CASE(TestC14_GracefulStartupAndShutdown) {
    MatchingEngine engine;
    ASSERT_TRUE(!engine.isRunning());

    engine.start();
    ASSERT_TRUE(engine.isRunning());

    // Submit orders and immediately call stop with drain=true
    for (int i = 1; i <= 20; ++i) {
        engine.submitNewOrder(std::make_shared<Order>(OrderType::Limit, i, Side::Buy, 90 + i, 5));
    }

    engine.stop(true);
    ASSERT_TRUE(!engine.isRunning());
    ASSERT_EQ(engine.getLastProcessedSequence(), 20u);
}

// 15. Market and FAK Orders via Concurrent Engine
TEST_CASE(TestC15_MarketAndFakViaConcurrentEngine) {
    MatchingEngine engine;
    engine.start();

    // Resting liquidity: 10 @ 100, 10 @ 101
    engine.submitNewOrderSync(std::make_shared<Order>(OrderType::Limit, 1, Side::Sell, 100, 10)).get();
    engine.submitNewOrderSync(std::make_shared<Order>(OrderType::Limit, 2, Side::Sell, 101, 10)).get();

    // Market Buy 15
    auto mktFut = engine.submitNewOrderSync(std::make_shared<Order>(3, Side::Buy, 15));
    auto mktRes = mktFut.get();
    ASSERT_EQ(mktRes.executedTrades.size(), 2u);
    ASSERT_EQ(mktRes.executedTrades[0].getPrice(), 100);
    ASSERT_EQ(mktRes.executedTrades[0].getQuantity(), 10);
    ASSERT_EQ(mktRes.executedTrades[1].getPrice(), 101);
    ASSERT_EQ(mktRes.executedTrades[1].getQuantity(), 5);

    // FAK Buy 10 @ 101 (only 5 remain on Ask @ 101)
    auto fakFut = engine.submitNewOrderSync(std::make_shared<Order>(OrderType::FillAndKill, 4, Side::Buy, 101, 10));
    auto fakRes = fakFut.get();
    ASSERT_EQ(fakRes.executedTrades.size(), 1u);
    ASSERT_EQ(fakRes.executedTrades[0].getQuantity(), 5);

    // Book should now be completely empty
    auto snap = engine.getSnapshot();
    ASSERT_TRUE(snap->getbids().empty());
    ASSERT_TRUE(snap->getasks().empty());

    engine.stop(true);
}

/* -----------------------------------------------------
   Main Test Runner
   ----------------------------------------------------- */

int main() {
    std::cout << "=========================================================\n";
    std::cout << "  Matching Engine Concurrency Test Suite (Phase 2)\n";
    std::cout << "=========================================================\n";
    std::cout << "Total Concurrency Tests Executed: " << g_concTotalTests << "\n";
    std::cout << "Passed: " << g_concPassedTests << "\n";
    std::cout << "Failed: " << g_concFailedTests << "\n";
    std::cout << "=========================================================\n";

    if (g_concFailedTests == 0) {
        std::cout << "ALL " << g_concPassedTests << " CONCURRENCY TESTS PASSED SUCCESSFULLY!\n";
        return 0;
    } else {
        std::cerr << "FAILURES DETECTED: " << g_concFailedTests << " concurrency tests failed!\n";
        return 1;
    }
}
