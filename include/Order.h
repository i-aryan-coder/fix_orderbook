#ifndef ORDER_H
#define ORDER_H

#include <vector>
#include <list>
#include <memory>
#include <string>
#include <stdexcept>
#include <numeric>
#include <iostream>

/* -----------------------------------------------------
   Basic Types & Enumerations
   ----------------------------------------------------- */

enum class OrderType {
    Limit,
    Market,
    FillAndKill,
    fillandkill = FillAndKill  // Alias for backward compatibility
};
using ordertype = OrderType;

enum class Side {
    Buy,
    Sell
};

/* -----------------------------------------------------
   Level Info & Aggregated Order Book (Display / Snapshot)
   ----------------------------------------------------- */

struct LevelInfo {
    int price{0};
    int quantity{0};
};
using Levelinfo = LevelInfo;
using Levelinfos = std::vector<LevelInfo>;

class AggregatedOrderbook {
public:
    AggregatedOrderbook(const Levelinfos& bids, const Levelinfos& asks)
        : bids_(bids), asks_(asks) {}

    const Levelinfos& getbids() const { return bids_; }
    const Levelinfos& getasks() const { return asks_; }

private:
    Levelinfos bids_;
    Levelinfos asks_;
};

/* -----------------------------------------------------
   Trade & Execution Information
   ----------------------------------------------------- */

struct TradeInfo {
    int id_{0};
    int pprice_{0};
    int quantity_{0};
};
using tradeinfo = TradeInfo;

/**
 * @brief Represents an executed match between a buyer and a seller.
 *
 * Core Matching Principle: Under price-time priority continuous double auction,
 * the incoming (aggressing) order executes at the resting order's limit price.
 * Every trade has exactly ONE single execution price.
 */
class Trade {
public:
    Trade(int buyOrderId, int sellOrderId, int executionPrice, int quantity)
        : buyOrderId_(buyOrderId),
          sellOrderId_(sellOrderId),
          executionPrice_(executionPrice),
          quantity_(quantity) {}

    int getBuyOrderId() const { return buyOrderId_; }
    int getSellOrderId() const { return sellOrderId_; }
    int getPrice() const { return executionPrice_; }
    int getQuantity() const { return quantity_; }

    // Compatibility accessors for legacy code / FixApp
    TradeInfo getbidtrade() const { return TradeInfo{buyOrderId_, executionPrice_, quantity_}; }
    TradeInfo getasktrade() const { return TradeInfo{sellOrderId_, executionPrice_, quantity_}; }

private:
    int buyOrderId_;
    int sellOrderId_;
    int executionPrice_;
    int quantity_;
};
using trade = Trade;
using trades = std::vector<Trade>;

/* -----------------------------------------------------
   Order Entity
   ----------------------------------------------------- */

class Order {
public:
    // Limit or FAK order constructor
    Order(OrderType otype, int id, Side side, int price, int quantity)
        : ordertype_(otype),
          id_(id),
          side_(side),
          price_(price),
          ini_quantity_(quantity),
          rem_quantity_(quantity) {
        if (quantity <= 0) {
            throw std::invalid_argument("Order quantity must be positive. Provided: " + std::to_string(quantity));
        }
        if (otype != OrderType::Market && price <= 0) {
            throw std::invalid_argument("Limit and FAK order price must be positive. Provided: " + std::to_string(price));
        }
    }

    // Market order constructor: price does not apply and is set to 0.
    Order(int id, Side side, int quantity)
        : Order(OrderType::Market, id, side, 0, quantity) {}

    int getorderid() const { return id_; }
    Side getside() const { return side_; }
    int getprice() const { return price_; }
    OrderType getordertype() const { return ordertype_; }
    int getini() const { return ini_quantity_; }
    int getrem() const { return rem_quantity_; }
    int getfilled() const { return ini_quantity_ - rem_quantity_; }
    bool isfilled() const { return rem_quantity_ == 0; }

    void fill(int quantity) {
        if (quantity <= 0) {
            throw std::invalid_argument("Fill quantity must be positive. Provided: " + std::to_string(quantity));
        }
        if (quantity > rem_quantity_) {
            throw std::runtime_error("Overfill error: attempted to fill " +
                std::to_string(quantity) + " but remaining is " + std::to_string(rem_quantity_));
        }
        rem_quantity_ -= quantity;
    }

    // In-place modification of remaining quantity (used when modifying quantity)
    void setRemainingQuantity(int newRem) {
        if (newRem <= 0) {
            throw std::invalid_argument("Remaining quantity must be positive. Provided: " + std::to_string(newRem));
        }
        int filled = ini_quantity_ - rem_quantity_;
        rem_quantity_ = newRem;
        ini_quantity_ = filled + newRem;
    }

private:
    OrderType ordertype_;
    int id_;
    Side side_;
    int price_;
    int ini_quantity_;
    int rem_quantity_;
};

using orderptr = std::shared_ptr<Order>;
using orderpointer = std::list<orderptr>;

/* -----------------------------------------------------
   Order Modification Request
   ----------------------------------------------------- */

class OrderModify {
public:
    OrderModify(int id, Side side, int price, int quantity)
        : id_(id), side_(side), price_(price), quantity_(quantity) {
        if (quantity <= 0) {
            throw std::invalid_argument("Modified quantity must be positive. Provided: " + std::to_string(quantity));
        }
        if (price <= 0) {
            throw std::invalid_argument("Modified price must be positive. Provided: " + std::to_string(price));
        }
    }

    int getorderid() const { return id_; }
    Side getside() const { return side_; }
    int getprice() const { return price_; }
    int getquantity() const { return quantity_; }

    orderptr toorderptr(OrderType type) const {
        return std::make_shared<Order>(type, id_, side_, price_, quantity_);
    }

private:
    int id_;
    Side side_;
    int price_;
    int quantity_;
};
using ordermodify = OrderModify;

#endif // ORDER_H
