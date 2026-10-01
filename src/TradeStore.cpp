#include "TradeStore.h"

#include <chrono>

TradeStore::TradeStore(size_t capacity) : capacity_(capacity == 0 ? 1 : capacity) {}

ApiTradeRecord TradeStore::recordTrade(const Trade& trade) {
    std::lock_guard<std::mutex> lock(mutex_);
    ApiTradeRecord record;
    record.tradeId = nextTradeId_++;
    record.buyOrderId = trade.getBuyOrderId();
    record.sellOrderId = trade.getSellOrderId();
    record.price = trade.getPrice();
    record.quantity = trade.getQuantity();
    record.timestampNanos = nowNanos();

    if (trades_.size() >= capacity_) {
        trades_.pop_front();
    }
    trades_.push_back(record);
    return record;
}

std::vector<ApiTradeRecord> TradeStore::recentTrades() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::vector<ApiTradeRecord>(trades_.begin(), trades_.end());
}

size_t TradeStore::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return trades_.size();
}

uint64_t TradeStore::nowNanos() {
    auto now = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}
