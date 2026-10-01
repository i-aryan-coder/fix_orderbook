#ifndef ORDERBOOKSNAPSHOT_H
#define ORDERBOOKSNAPSHOT_H

#include "Order.h"
#include <chrono>
#include <cstdint>
#include <vector>

/**
 * @brief Immutable, read-only snapshot of the order book.
 *
 * External consumers (REST APIs, WebSockets, dashboards, monitoring) inspect
 * this snapshot without holding locks on the matching engine or mutating
 * the canonical Orderbook.
 */
class OrderbookSnapshot {
public:
    OrderbookSnapshot()
        : sequenceNumber_(0),
          timestamp_(std::chrono::system_clock::now()) {}

    OrderbookSnapshot(uint64_t seq,
                      std::chrono::system_clock::time_point ts,
                      Levelinfos bids,
                      Levelinfos asks)
        : sequenceNumber_(seq),
          timestamp_(ts),
          bids_(std::move(bids)),
          asks_(std::move(asks)) {}

    OrderbookSnapshot(uint64_t seq,
                      std::chrono::system_clock::time_point ts,
                      const AggregatedOrderbook& agg)
        : sequenceNumber_(seq),
          timestamp_(ts),
          bids_(agg.getbids()),
          asks_(agg.getasks()) {}

    uint64_t getSequenceNumber() const { return sequenceNumber_; }
    std::chrono::system_clock::time_point getTimestamp() const { return timestamp_; }
    const Levelinfos& getbids() const { return bids_; }
    const Levelinfos& getasks() const { return asks_; }

private:
    uint64_t sequenceNumber_{0};
    std::chrono::system_clock::time_point timestamp_;
    Levelinfos bids_;
    Levelinfos asks_;
};

#endif // ORDERBOOKSNAPSHOT_H
