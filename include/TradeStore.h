#ifndef TRADESTORE_H
#define TRADESTORE_H

#include "ApiModels.h"

#include <cstddef>
#include <deque>
#include <mutex>
#include <vector>

class TradeStore {
public:
    explicit TradeStore(size_t capacity = 1000);

    ApiTradeRecord recordTrade(const Trade& trade);
    std::vector<ApiTradeRecord> recentTrades() const;
    size_t size() const;

private:
    static uint64_t nowNanos();

    size_t capacity_;
    uint64_t nextTradeId_{1};
    std::deque<ApiTradeRecord> trades_;
    mutable std::mutex mutex_;
};

#endif // TRADESTORE_H
