#ifndef ORDERBOOK_H
#define ORDERBOOK_H

#include <bits/stdc++.h>
using namespace std;

enum class ordertype { Limit, Market, fillandkill };
enum class Side { Buy, Sell };

struct Levelinfo {
    int price;
    int quantity;
};
using Levelinfos = vector<Levelinfo>;

// Aggregated order book for display purposes.
class AggregatedOrderbook {
public:
    AggregatedOrderbook(const Levelinfos& bids, const Levelinfos& asks)
        : bids_(bids), asks_(asks) {
    }
    const Levelinfos& getbids() const { return bids_; }
    const Levelinfos& getasks() const { return asks_; }
private:
    Levelinfos bids_;
    Levelinfos asks_;
};

class Order {
public:
    Order(ordertype otype, int id, Side side, int price, int quantity)
        : ordertype_(otype), id_(id), side_(side), price_(price),
        ini_quantity_(quantity), rem_quantity_(quantity) {
    }
    // Market order constructor: price is set to -1.
    Order(int id, Side side, int quantity)
        : Order(ordertype::Market, id, side, -1, quantity) {
    }

    int getorderid() const { return id_; }
    Side getside() const { return side_; }
    int getprice() const { return price_; }
    ordertype getordertype() const { return ordertype_; }
    int getini() const { return ini_quantity_; }
    int getrem() const { return rem_quantity_; }
    int getfilled() const { return getini() - getrem(); }
    bool isfilled() const { return getrem() == 0; }

    void fill(int quantity) {
        if (quantity > getrem())
            throw runtime_error("Overfill error");
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

using orderptr = shared_ptr<Order>;
using orderpointer = list<orderptr>;

class ordermodify {
public:
    ordermodify(int id, Side side, int price, int quantity)
        : id_(id), side_(side), price_(price), quantity_(quantity) {
    }
    int getorderid() const { return id_; }
    Side getside() const { return side_; }
    int getprice() const { return price_; }
    int getquantity() const { return quantity_; }
    orderptr toorderptr(ordertype type) const {
        return make_shared<Order>(type, getorderid(), getside(), getprice(), getquantity());
    }
private:
    int id_;
    int price_;
    int quantity_;
    Side side_;
};

struct tradeinfo {
    int id_;
    int pprice_;
    int quantity_;
};

class trade {
public:
    trade(const tradeinfo& bidtrade, const tradeinfo& asktrade)
        : bidtrade_(bidtrade), asktrade_(asktrade) {
    }
    const tradeinfo& getbidtrade() const { return bidtrade_; }
    const tradeinfo& getasktrade() const { return asktrade_; }
private:
    tradeinfo bidtrade_;
    tradeinfo asktrade_;
};

using trades = vector<trade>;

class Orderbook {
private:
    struct orderentry {
        orderptr order_{ nullptr };
        orderpointer::iterator location_;
    };

    // Bids: descending order, Asks: ascending order.
    map<int, orderpointer, greater<int>> bids_;
    map<int, orderpointer, less<int>> asks_;

    // Fast lookup by order ID.
    unordered_map<int, orderentry> orders_;

    // Check if order can immediately match.
    bool canmatch(Side side, int price_) const {
        if (side == Side::Buy) {
            if (asks_.empty()) return false;
            auto it = asks_.begin();
            return price_ >= it->first;
        }
        else {
            if (bids_.empty()) return false;
            auto it = bids_.begin();
            return price_ <= it->first;
        }
    }

    // Core matching algorithm.
    trades matchorder() {
        trades trades_;
        trades_.reserve(orders_.size());

        while (true) {
            if (bids_.empty() || asks_.empty()) break;
            auto it1 = bids_.begin();
            int bidsprice = it1->first;
            auto& bids = it1->second;
            auto it2 = asks_.begin();
            int asksprice = it2->first;
            auto& asks = it2->second;

            if (bidsprice < asksprice) break;

            while (!bids.empty() && !asks.empty()) {
                auto bid = bids.front();
                auto ask = asks.front();

                int quantity = min(bid->getrem(), ask->getrem());
                bid->fill(quantity);
                ask->fill(quantity);

                trades_.push_back(trade(
                    tradeinfo{ bid->getorderid(), bid->getprice(), quantity },
                    tradeinfo{ ask->getorderid(), ask->getprice(), quantity }
                ));

                if (bid->isfilled()) {
                    bids.pop_front();
                    orders_.erase(bid->getorderid());
                }
                if (ask->isfilled()) {
                    asks.pop_front();
                    orders_.erase(ask->getorderid());
                }
                if (bids.empty()) bids_.erase(bidsprice);
                if (asks.empty()) asks_.erase(asksprice);
            }
        }
        return trades_;
    }

public:
    Orderbook() = default;

    trades addorder(orderptr order) {
        if (orders_.find(order->getorderid()) != orders_.end())
            return {};
        if (order->getordertype() == ordertype::fillandkill && !canmatch(order->getside(), order->getprice()))
            return {};
        orderpointer::iterator it;
        if (order->getside() == Side::Buy) {
            auto& ordersAtPrice = bids_[order->getprice()];
            ordersAtPrice.push_back(order);
            it = prev(ordersAtPrice.end());
        }
        else {
            auto& ordersAtPrice = asks_[order->getprice()];
            ordersAtPrice.push_back(order);
            it = prev(ordersAtPrice.end());
        }
        orders_.insert({ order->getorderid(), orderentry{order, it} });
        return matchorder();
    }

    void cancelorder(int id) {
        if (orders_.find(id) == orders_.end()) return;
        const auto& [order, it] = orders_.at(id);
        orders_.erase(id);
        if (order->getside() == Side::Sell) {
            int price = order->getprice();
            auto& ordersAtPrice = asks_.at(price);
            ordersAtPrice.erase(it);
            if (ordersAtPrice.empty())
                asks_.erase(price);
        }
        else {
            int price = order->getprice();
            auto& ordersAtPrice = bids_.at(price);
            ordersAtPrice.erase(it);
            if (ordersAtPrice.empty())
                bids_.erase(price);
        }
    }

    trades Matchorder(const ordermodify& omod) {
        if (orders_.find(omod.getorderid()) == orders_.end())
            return {};
        auto entry = orders_.at(omod.getorderid());
        auto oldOrder = entry.order_;
        cancelorder(omod.getorderid());
        return addorder(omod.toorderptr(oldOrder->getordertype()));
    }

    AggregatedOrderbook getorderinfo() const {
        Levelinfos bidinfo, askinfo;
        bidinfo.reserve(orders_.size());
        askinfo.reserve(orders_.size());
        auto aggregator = [](int price, const orderpointer& ops) {
            int total = accumulate(ops.begin(), ops.end(), 0,
                [](int sum, const orderptr& o) { return sum + o->getrem(); });
            return Levelinfo{ price, total };
            };
        for (auto& [price, optrs] : bids_) {
            bidinfo.push_back(aggregator(price, optrs));
        }
        for (auto& [price, optrs] : asks_) {
            askinfo.push_back(aggregator(price, optrs));
        }
        return AggregatedOrderbook(bidinfo, askinfo);
    }
};

#endif // ORDERBOOK_H
#pragma once
