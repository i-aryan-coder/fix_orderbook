#ifndef ORDERBOOK_H
#define ORDERBOOK_H

#include "Order.h"
#include <map>
#include <unordered_map>
#include <list>
#include <vector>
#include <memory>

/**
 * @brief High-performance single-instrument Orderbook matching engine.
 *
 * Architecture & Invariants:
 * 1. Price-Time Priority:
 *    - Bids sorted in strictly descending order (std::greater<int>). Best bid is bids_.begin().
 *    - Asks sorted in strictly ascending order (std::less<int>). Best ask is asks_.begin().
 *    - At each price level, orders are stored in a FIFO queue (std::list<orderptr>).
 * 2. Order Lookup Invariant:
 *    - orders_ contains every resting order. Erasing from orders_ and price list is atomic.
 * 3. Spread Invariant:
 *    - Resting bids and asks never cross. If both non-empty: best_bid < best_ask.
 * 4. Execution Price Invariant:
 *    - Aggressing orders execute at the resting order's limit price.
 * 5. Lifecycle Semantics:
 *    - Market orders: Aggress opposite book, fill as much as possible, never rest.
 *    - FAK (Fill-and-Kill): Aggress opposite book up to limit, unexecuted remainder cancelled, never rest.
 *    - Limit orders: Aggress crossing orders; unexecuted remainder rests in the book.
 * 6. Modification Policy:
 *    - Quantity reduction at same price: Preserves queue time priority (in-place modification).
 *    - Quantity increase at same price: Loses time priority (moved to end of queue).
 *    - Price change: Loses time priority (cancelled and re-submitted).
 */
class Orderbook {
public:
    Orderbook() = default;
    ~Orderbook() = default;

    // Disallow copying to avoid accidental deep state duplication
    Orderbook(const Orderbook&) = delete;
    Orderbook& operator=(const Orderbook&) = delete;
    Orderbook(Orderbook&&) noexcept = default;
    Orderbook& operator=(Orderbook&&) noexcept = default;

    // Core Matching Engine API
    trades addorder(orderptr order);
    void cancelorder(int id);
    trades Matchorder(const ordermodify& omod);
    trades modifyorder(const ordermodify& omod) { return Matchorder(omod); }

    // Inspection & Snapshot API
    AggregatedOrderbook getorderinfo() const;
    bool hasorder(int id) const;
    orderptr getorder(int id) const;
    size_t ordercount() const;
    bool hasBidLevel(int price) const;
    bool hasAskLevel(int price) const;
    size_t bidLevelCount() const;
    size_t askLevelCount() const;

private:
    struct orderentry {
        orderptr order_{nullptr};
        orderpointer::iterator location_;
    };

    // Price level books
    std::map<int, orderpointer, std::greater<int>> bids_;
    std::map<int, orderpointer, std::less<int>> asks_;

    // Fast order-ID lookup
    std::unordered_map<int, orderentry> orders_;

    // Validations
    void validateNewOrder(const orderptr& order) const;
    void validateModify(const ordermodify& omod) const;

    // Internal matching routines
    trades matchMarket(orderptr order);
    trades matchFak(orderptr order);
    trades matchLimit(orderptr order);
};

#endif // ORDERBOOK_H
