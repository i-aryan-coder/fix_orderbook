#include "MatchingEngine.h"
#include "Order.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct BenchmarkResult {
    std::string name;
    size_t operations{0};
    double elapsedMs{0.0};
    double throughputOpsPerSec{0.0};
    double averageLatencyUs{0.0};
    double p50Us{0.0};
    double p95Us{0.0};
    double p99Us{0.0};
};

int readWorkloadSize() {
    const char* value = std::getenv("BENCH_OPS");
    if (value == nullptr) {
        return 10000;
    }
    try {
        int parsed = std::stoi(value);
        return parsed > 0 ? parsed : 10000;
    } catch (...) {
        return 10000;
    }
}

double percentile(std::vector<double>& values, double percentileValue) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const size_t index = static_cast<size_t>((percentileValue / 100.0) * static_cast<double>(values.size() - 1));
    return values[index];
}

BenchmarkResult buildResult(const std::string& name, size_t operations, Clock::time_point start,
                            Clock::time_point end, std::vector<double> latenciesUs) {
    const double elapsedMs = std::chrono::duration<double, std::milli>(end - start).count();
    const double totalLatency = std::accumulate(latenciesUs.begin(), latenciesUs.end(), 0.0);
    BenchmarkResult result;
    result.name = name;
    result.operations = operations;
    result.elapsedMs = elapsedMs;
    result.throughputOpsPerSec = elapsedMs > 0.0 ? (static_cast<double>(operations) * 1000.0) / elapsedMs : 0.0;
    result.averageLatencyUs = latenciesUs.empty() ? 0.0 : totalLatency / static_cast<double>(latenciesUs.size());
    result.p50Us = percentile(latenciesUs, 50.0);
    result.p95Us = percentile(latenciesUs, 95.0);
    result.p99Us = percentile(latenciesUs, 99.0);
    return result;
}

orderptr limitOrder(int id, Side side, int price, int quantity = 1) {
    return std::make_shared<Order>(OrderType::Limit, id, side, price, quantity);
}

orderptr marketOrder(int id, Side side, int quantity = 1) {
    return std::make_shared<Order>(id, side, quantity);
}

BenchmarkResult benchmarkSyncNewOrders(int operations) {
    MatchingEngine engine;
    engine.start();
    std::vector<double> latencies;
    latencies.reserve(static_cast<size_t>(operations));

    const auto start = Clock::now();
    for (int i = 0; i < operations; ++i) {
        const auto opStart = Clock::now();
        auto result = engine.submitNewOrderSync(limitOrder(1000000 + i, Side::Buy, 90 + (i % 10))).get();
        const auto opEnd = Clock::now();
        if (!result.success) {
            throw std::runtime_error("new order benchmark rejected an order");
        }
        latencies.push_back(std::chrono::duration<double, std::micro>(opEnd - opStart).count());
    }
    const auto end = Clock::now();
    engine.stop(true);
    return buildResult("new_order_non_crossing_limit", static_cast<size_t>(operations), start, end, std::move(latencies));
}

BenchmarkResult benchmarkCancelOrders(int operations) {
    MatchingEngine engine;
    engine.start();
    for (int i = 0; i < operations; ++i) {
        engine.submitNewOrderSync(limitOrder(2000000 + i, Side::Buy, 80 + (i % 10))).get();
    }

    std::vector<double> latencies;
    latencies.reserve(static_cast<size_t>(operations));
    const auto start = Clock::now();
    for (int i = 0; i < operations; ++i) {
        const auto opStart = Clock::now();
        auto result = engine.submitCancelOrderSync(2000000 + i).get();
        const auto opEnd = Clock::now();
        if (!result.success) {
            throw std::runtime_error("cancel benchmark failed to cancel an order");
        }
        latencies.push_back(std::chrono::duration<double, std::micro>(opEnd - opStart).count());
    }
    const auto end = Clock::now();
    engine.stop(true);
    return buildResult("cancel_resting_orders", static_cast<size_t>(operations), start, end, std::move(latencies));
}

BenchmarkResult benchmarkModifyOrders(int operations) {
    MatchingEngine engine;
    engine.start();
    for (int i = 0; i < operations; ++i) {
        engine.submitNewOrderSync(limitOrder(3000000 + i, Side::Buy, 70 + (i % 10), 10)).get();
    }

    std::vector<double> latencies;
    latencies.reserve(static_cast<size_t>(operations));
    const auto start = Clock::now();
    for (int i = 0; i < operations; ++i) {
        const auto opStart = Clock::now();
        auto result = engine.submitModifyOrderSync(OrderModify(3000000 + i, Side::Buy, 70 + (i % 10), 5)).get();
        const auto opEnd = Clock::now();
        if (!result.success) {
            throw std::runtime_error("modify benchmark failed to modify an order");
        }
        latencies.push_back(std::chrono::duration<double, std::micro>(opEnd - opStart).count());
    }
    const auto end = Clock::now();
    engine.stop(true);
    return buildResult("modify_quantity_reduction", static_cast<size_t>(operations), start, end, std::move(latencies));
}

BenchmarkResult benchmarkCrossingMatches(int operations) {
    MatchingEngine engine;
    engine.start();
    for (int i = 0; i < operations; ++i) {
        engine.submitNewOrderSync(limitOrder(4000000 + i, Side::Sell, 100, 1)).get();
    }

    std::vector<double> latencies;
    latencies.reserve(static_cast<size_t>(operations));
    const auto start = Clock::now();
    for (int i = 0; i < operations; ++i) {
        const auto opStart = Clock::now();
        auto result = engine.submitNewOrderSync(limitOrder(5000000 + i, Side::Buy, 100, 1)).get();
        const auto opEnd = Clock::now();
        if (!result.success || result.executedTrades.empty()) {
            throw std::runtime_error("crossing benchmark failed to match");
        }
        latencies.push_back(std::chrono::duration<double, std::micro>(opEnd - opStart).count());
    }
    const auto end = Clock::now();
    engine.stop(true);
    return buildResult("continuous_crossing_limit_matches", static_cast<size_t>(operations), start, end, std::move(latencies));
}

BenchmarkResult benchmarkMarketOrders(int operations) {
    MatchingEngine engine;
    engine.start();
    for (int i = 0; i < operations; ++i) {
        engine.submitNewOrderSync(limitOrder(6000000 + i, Side::Sell, 100 + (i % 5), 1)).get();
    }

    std::vector<double> latencies;
    latencies.reserve(static_cast<size_t>(operations));
    const auto start = Clock::now();
    for (int i = 0; i < operations; ++i) {
        const auto opStart = Clock::now();
        auto result = engine.submitNewOrderSync(marketOrder(7000000 + i, Side::Buy, 1)).get();
        const auto opEnd = Clock::now();
        if (!result.success || result.executedTrades.empty()) {
            throw std::runtime_error("market order benchmark failed to consume liquidity");
        }
        latencies.push_back(std::chrono::duration<double, std::micro>(opEnd - opStart).count());
    }
    const auto end = Clock::now();
    engine.stop(true);
    return buildResult("market_orders_consuming_liquidity", static_cast<size_t>(operations), start, end, std::move(latencies));
}

BenchmarkResult benchmarkConcurrentProducers(int operations) {
    const unsigned int hardwareThreads = std::max(2U, std::thread::hardware_concurrency());
    const int producerCount = static_cast<int>(std::min(8U, hardwareThreads));
    const int perProducer = std::max(1, operations / producerCount);
    const int totalOperations = perProducer * producerCount;

    MatchingEngine engine;
    engine.start();
    std::vector<std::thread> producers;
    producers.reserve(static_cast<size_t>(producerCount));
    std::vector<uint64_t> finalSequences(static_cast<size_t>(producerCount), 0);

    const auto start = Clock::now();
    for (int producer = 0; producer < producerCount; ++producer) {
        producers.emplace_back([producer, perProducer, &engine, &finalSequences]() {
            uint64_t lastSeq = 0;
            for (int i = 0; i < perProducer; ++i) {
                const int id = 8000000 + producer * perProducer + i;
                lastSeq = engine.submitNewOrder(limitOrder(id, Side::Buy, 60 + (i % 10), 1));
            }
            finalSequences[static_cast<size_t>(producer)] = lastSeq;
        });
    }

    for (auto& producer : producers) {
        producer.join();
    }
    const uint64_t maxSequence = *std::max_element(finalSequences.begin(), finalSequences.end());
    engine.waitForSequence(maxSequence);
    const auto end = Clock::now();
    engine.stop(true);

    return buildResult("concurrent_mpsc_producers", static_cast<size_t>(totalOperations), start, end, {});
}

void printResult(const BenchmarkResult& result) {
    std::cout << std::left << std::setw(38) << result.name
              << " ops=" << std::setw(8) << result.operations
              << " elapsed_ms=" << std::fixed << std::setprecision(3) << std::setw(10) << result.elapsedMs
              << " throughput_ops_sec=" << std::setw(12) << result.throughputOpsPerSec;
    if (result.averageLatencyUs > 0.0) {
        std::cout << " avg_us=" << std::setw(9) << result.averageLatencyUs
                  << " p50_us=" << std::setw(9) << result.p50Us
                  << " p95_us=" << std::setw(9) << result.p95Us
                  << " p99_us=" << std::setw(9) << result.p99Us;
    }
    std::cout << '\n';
}

} // namespace

int main() {
    try {
        const int operations = readWorkloadSize();
        std::cout << "Trading Engine benchmark suite\n";
        std::cout << "BENCH_OPS=" << operations << "\n";
        std::cout << "Measurements include the existing MatchingEngine event queue and worker thread.\n";

        std::vector<BenchmarkResult> results;
        results.push_back(benchmarkSyncNewOrders(operations));
        results.push_back(benchmarkCancelOrders(operations));
        results.push_back(benchmarkModifyOrders(operations));
        results.push_back(benchmarkCrossingMatches(operations));
        results.push_back(benchmarkMarketOrders(operations));
        results.push_back(benchmarkConcurrentProducers(operations));

        for (const auto& result : results) {
            printResult(result);
        }
    } catch (const std::exception& e) {
        std::cerr << "Benchmark failed: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
