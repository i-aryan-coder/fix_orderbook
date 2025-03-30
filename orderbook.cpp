#include <bits/stdc++.h>
#include "quickfix/Application.h"

using namespace std;

/* --------------------------
   Basic Types & Structures
   -------------------------- */
enum class ordertype { Limit, Market, fillandkill };
enum class Side { Buy, Sell };

struct Levelinfo {
    int price;
    int quantity;
};
using Levelinfos = vector<Levelinfo>;

/* -----------------------------------------------------
   Aggregated Order Book Data Holder (for display purposes)
   ----------------------------------------------------- */
class AggregatedOrderbook {
public:
    AggregatedOrderbook(const Levelinfos& bids, const Levelinfos& asks)
        : bids_{bids}, asks_{asks} {}

    const Levelinfos& getbids() const { return bids_; }
    const Levelinfos& getasks() const { return asks_; }

private:
    Levelinfos bids_;
    Levelinfos asks_;
};

/* ----------------
   Order Class
   ---------------- */
class Order {
public:
    Order(ordertype otype, int id, Side side, int price, int quantity)
        : ordertype_{otype},
          id_{id},
          side_{side},
          price_{price},
          ini_quantity_{quantity},
          rem_quantity_{quantity}
    {}
    // Market order constructor: price is set to -1
    Order(int id, Side side, int quantity)
        : Order(ordertype::Market, id, side, -1, quantity)
    {}

    int getorderid() const         { return id_; }
    Side getside() const           { return side_; }
    int getprice() const           { return price_; }
    ordertype getordertype() const { return ordertype_; }
    int getini() const             { return ini_quantity_; }
    int getrem() const             { return rem_quantity_; }
    int getfilled() const          { return getini() - getrem(); }
    
    bool isfilled() const          { return getrem() == 0; }

    void fill(int quantity) {
        if (quantity > getrem()) {
            throw 404;
        }
        rem_quantity_ -= quantity;
    }
    
private:
    ordertype ordertype_;
    int id_;
    Side side_;
    int price_;
    int ini_quantity_;
    int rem_quantity_;
};

/* --------------------------------
   Shared Pointer & Container Types
   -------------------------------- */
//making use of list as comapred to vector due to the reason that it do deletion and addition form middle in O(1) instead of O(n) 
//using a shared pointer instead of normal pointer to automate memory management
using orderptr     = shared_ptr<Order>;
using orderpointer = list<orderptr>;

/* -----------------------
   Order Modify Class
   ----------------------- */
class ordermodify {
public:
    ordermodify(int id, Side side, int price, int quantity)
        : id_{id}, side_{side}, price_{price}, quantity_{quantity}
    {}

    int getorderid() const  { return id_; }
    Side getside() const    { return side_; }
    int getprice() const    { return price_; }
    int getquantity() const { return quantity_; }

    // Creates a new Order pointer from modified info, preserving order type.
    orderptr toorderptr(ordertype type) const {
        return make_shared<Order>(type, getorderid(), getside(), getprice(), getquantity());
    }
private:
    int id_;
    int price_;
    int quantity_;
    Side side_;
};

/* -----------
   Trade Info
   ----------- */
struct tradeinfo {
    int id_;
    int pprice_;
    int quantity_;
};

class trade {
public:
    trade(const tradeinfo& bidtrade, const tradeinfo& asktrade)
        : bidtrade_{bidtrade}, asktrade_{asktrade} {}

    const tradeinfo& getbidtrade() const { return bidtrade_; }
    const tradeinfo& getasktrade() const { return asktrade_; }

private:
    tradeinfo bidtrade_;
    tradeinfo asktrade_;
};

using trades = vector<trade>;

/* --------------------------------------------------
   Main Matching Engine: Orderbook (Matching Engine)
   -------------------------------------------------- */
class Orderbook {
private:
    struct orderentry {
        orderptr order_{nullptr};
        orderpointer::iterator location_; // Points to the order in the list
    };

    // Bids sorted in descending order, Asks in ascending order
    map<int, orderpointer, greater<int>> bids_;//;//beacuse we have to make the orders match so made it in descending order
    map<int, orderpointer, less<int>>asks_;//O(logn)for insertion and deletion in map)

    // Fast lookup by order ID
    unordered_map<int, orderentry> orders_;//to make operations in O(1) which are not priority heavy 

    // Checks if a new order can match immediately.
    bool canmatch(Side side, int price_) const {
        if (side == Side::Buy) {
            if (asks_.empty()) return false;
            auto it = asks_.begin(); // lowest ask
            return price_ >= it->first;
        } else { // Sell order
            if (bids_.empty()) return false;
            auto it = bids_.begin(); // highest bid
            return price_ <= it->first;
        }
    }

    // Core matching algorithm: matches orders at the best price levels (FIFO within each level) 
    // Time complexity for matching algorithm is O(nlogn)
    trades matchorder() {
        trades trades_;
        // Reserve space for potential trades
        trades_.reserve(orders_.size());

        while (true) {
            if (bids_.empty() || asks_.empty()) break;
            auto it1 = bids_.begin();
            int bidsprice = it1->first;
            auto &bids = it1->second;
            auto it2 = asks_.begin();
            int asksprice = it2->first;
            auto &asks = it2->second;

            // If best bid price is less than best ask price, no match is possible.
            if (bidsprice < asksprice) break;

            // While there are orders at these levels, match them.
            while (!bids.empty() && !asks.empty()) {
                auto bid = bids.front();
                auto ask = asks.front();

                int quantity = min(bid->getrem(), ask->getrem());
                bid->fill(quantity);
                ask->fill(quantity);

                trades_.push_back(trade(
                    tradeinfo{bid->getorderid(), bid->getprice(), quantity},
                    tradeinfo{ask->getorderid(), ask->getprice(), quantity}
                ));

                // Remove fully filled orders
                if (bid->isfilled()) {
                    bids.pop_front();
                    orders_.erase(bid->getorderid());
                }
                if (ask->isfilled()) {
                    asks.pop_front();
                    orders_.erase(ask->getorderid());
                }

                // If the price level becomes empty, remove it from the map.
                if (bids.empty()) bids_.erase(bidsprice);
                if (asks.empty()) asks_.erase(asksprice);
            }
        }

        // Optional: Cancel any remaining fill-and-kill orders at the best price level.
        if (!bids_.empty()) {
            auto it = bids_.begin();
            auto &order = it->second.front();
            if (order->getordertype() == ordertype::fillandkill) {
                cancelorder(order->getorderid());
            }
        }
        if (!asks_.empty()) {
            auto it = asks_.begin();
            auto &order = it->second.front();
            if (order->getordertype() == ordertype::fillandkill) {
                cancelorder(order->getorderid());
            }
        }

        return trades_;
    }

public:
    // Default constructor
    Orderbook() = default;

    // Add a new order and attempt to match.
    trades addorder(orderptr order) {
        // Prevent duplicate order IDs.
        if (orders_.find(order->getorderid()) != orders_.end()) {
            return {};
        }

        // If a fill-and-kill order cannot match immediately, discard it.
        if (order->getordertype() == ordertype::fillandkill &&
            !canmatch(order->getside(), order->getprice()))
        {
            return {};
        }

        // Insert order into bids or asks based on side.
        orderpointer::iterator it;
        if (order->getside() == Side::Buy) {
            auto &it1= bids_[order->getprice()];
            it1.push_back(order);
            it = prev(it1.end()); // Iterator to the newly inserted order.
        } else {
            auto &it1 = asks_[order->getprice()];
            it1.push_back(order);
            it = prev(it1.end());
        }

        // Track the order globally.
        orders_.insert({order->getorderid(), orderentry{order, it}});

        // Attempt to match orders after adding this one.
        return matchorder();
    }

    // Cancel an existing order.
    void cancelorder(int id) {
        if (orders_.find(id) == orders_.end()) return;

        const auto &[order, it] = orders_.at(id);
        orders_.erase(id);

        if (order->getside() == Side::Sell) {
            int price = order->getprice();
            auto &it1 = asks_.at(price);
            it1.erase(it);
            if (it1.empty()) {
                asks_.erase(price);
            }
        } else {
            int price=order->getprice();
            auto &it1=bids_.at(price);
            it1.erase(it);
            if (it1.empty()) {
                bids_.erase(price);
            }
        }
    }

    // Modify an existing order by canceling and re-adding it.
    //Punishing the user for not using the correct order type.
    // This is a simple implementation;IN acutal orderbook we use remodify the order and keep its positon same in the
    trades Matchorder(const ordermodify &omod) {
        if (orders_.find(omod.getorderid()) == orders_.end()) {
            return {};
        }
        auto entry = orders_.at(omod.getorderid());
        auto oldOrder = entry.order_;
        cancelorder(omod.getorderid());
        return addorder(omod.toorderptr(oldOrder->getordertype()));
    }

    // Aggregate current bids and asks into an AggregatedOrderbook for display.
    AggregatedOrderbook getorderinfo() const {
        Levelinfos bidinfo, askinfo;
        // Reserve space (optional; orders_.size() may not match levels, but it's an estimate)
        bidinfo.reserve(orders_.size());
        askinfo.reserve(orders_.size());

        // Lambda to sum the remaining quantity at a price level.
        auto aggregator = [](int price, const orderpointer &ops) {
            int total = accumulate(ops.begin(), ops.end(), 0, 
                [](int sum, const orderptr &o) {
                    return sum + o->getrem();
                });
            return Levelinfo{price, total};
        };

        for (auto &[price, optrs] : bids_) {
            bidinfo.push_back(aggregator(price, optrs));
        }
        for (auto &[price, optrs] : asks_) {
            askinfo.push_back(aggregator(price, optrs));
        }
        return AggregatedOrderbook(bidinfo, askinfo);
    }
};

/* -----------------------
   Helper: Print Aggregated Order Book
   ----------------------- */
void printOrderBook(const AggregatedOrderbook &abook) {
    cout << "\n--- Order Book After Trade Execution ---\n";
    cout << "Bids (Buy Orders):\n";
    for (const auto &bid : abook.getbids()) {
        cout << "Price: " << bid.price << ", Quantity: " << bid.quantity << "\n";
    }
    cout << "\nAsks (Sell Orders):\n";
    for (const auto &ask : abook.getasks()) {
        cout << "Price: " << ask.price << ", Quantity: " << ask.quantity << "\n";
    }
    cout << "-------------------------------------\n";
}

/* -----------------------
   Main Function: Testing
   ----------------------- */
   int main() {
    Orderbook ob;  // Our matching engine

    // Sell Orders (sorted in ascending price order)
    ob.addorder(make_shared<Order>(ordertype::Limit, 1, Side::Sell, 100, 5));
    ob.addorder(make_shared<Order>(ordertype::Limit, 2, Side::Sell, 103, 5));
    ob.addorder(make_shared<Order>(ordertype::Limit, 3, Side::Sell, 105, 5));
    
    // Market Buy Order (should consume lowest sell orders)
    ob.addorder(make_shared<Order>(ordertype::Market, 4, Side::Buy,101, 8));ob.addorder(make_shared<Order>(ordertype::Limit, 5, Side::Buy, 102, 8));
    
    auto book = ob.getorderinfo();
    printOrderBook(book);
    /* Expected:
       Sell Orders:
       Price 105, Qty 5 (Unmatched remaining)
    */
    
}

/* -----------------------
   Notes:
   - The code implements a simple order book system with basic order types and matching logic.
   - It includes classes for orders, order modifications, and trade information.
   - The main function demonstrates adding orders and printing the order book.
   ----------------------- */   