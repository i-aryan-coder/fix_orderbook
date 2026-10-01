#include "Orderbook.h"
#include <algorithm>
#include <stdexcept>
#include <iostream>

/* -----------------------------------------------------
   Input Validation
   ----------------------------------------------------- */

void Orderbook::validateNewOrder(const orderptr& order) const {
    if (!order) {
        throw std::invalid_argument("Order cannot be null");
    }
    if (order->getini() <= 0) {
        throw std::invalid_argument("Order quantity must be positive. Provided: " + std::to_string(order->getini()));
    }
    if (orders_.find(order->getorderid()) != orders_.end()) {
        throw std::invalid_argument("Order ID already exists: " + std::to_string(order->getorderid()));
    }
    if (order->getordertype() != OrderType::Market) {
        if (order->getprice() <= 0) {
            throw std::invalid_argument("Limit and FAK order price must be positive. Provided: " + std::to_string(order->getprice()));
        }
    }
}

void Orderbook::validateModify(const ordermodify& omod) const {
    auto it = orders_.find(omod.getorderid());
    if (it == orders_.end()) {
        throw std::out_of_range("Order ID not found for modification: " + std::to_string(omod.getorderid()));
    }
    if (omod.getquantity() <= 0) {
        throw std::invalid_argument("Modified quantity must be positive. Provided: " + std::to_string(omod.getquantity()));
    }
    if (omod.getprice() <= 0) {
        throw std::invalid_argument("Modified price must be positive. Provided: " + std::to_string(omod.getprice()));
    }
    const auto& existingOrder = it->second.order_;
    if (existingOrder->getside() != omod.getside()) {
        throw std::invalid_argument("Cannot modify order side. Existing side: " +
            std::string(existingOrder->getside() == Side::Buy ? "Buy" : "Sell") +
            ", Requested: " + std::string(omod.getside() == Side::Buy ? "Buy" : "Sell"));
    }
}

/* -----------------------------------------------------
   Core Matching Routines
   ----------------------------------------------------- */

trades Orderbook::matchMarket(orderptr order) {
    trades tradeList;
    if (order->getside() == Side::Buy) {
        while (order->getrem() > 0 && !asks_.empty()) {
            auto bestAskIt = asks_.begin();
            auto& askList = bestAskIt->second;

            while (order->getrem() > 0 && !askList.empty()) {
                auto restingAsk = askList.front();
                int matchQty = std::min(order->getrem(), restingAsk->getrem());
                int execPrice = restingAsk->getprice(); // Executes at resting order's price

                order->fill(matchQty);
                restingAsk->fill(matchQty);

                tradeList.emplace_back(order->getorderid(), restingAsk->getorderid(), execPrice, matchQty);

                if (restingAsk->isfilled()) {
                    orders_.erase(restingAsk->getorderid());
                    askList.pop_front();
                }
            }

            if (askList.empty()) {
                asks_.erase(bestAskIt);
            }
        }
    } else { // Market Sell
        while (order->getrem() > 0 && !bids_.empty()) {
            auto bestBidIt = bids_.begin();
            auto& bidList = bestBidIt->second;

            while (order->getrem() > 0 && !bidList.empty()) {
                auto restingBid = bidList.front();
                int matchQty = std::min(order->getrem(), restingBid->getrem());
                int execPrice = restingBid->getprice(); // Executes at resting order's price

                order->fill(matchQty);
                restingBid->fill(matchQty);

                tradeList.emplace_back(restingBid->getorderid(), order->getorderid(), execPrice, matchQty);

                if (restingBid->isfilled()) {
                    orders_.erase(restingBid->getorderid());
                    bidList.pop_front();
                }
            }

            if (bidList.empty()) {
                bids_.erase(bestBidIt);
            }
        }
    }
    // Market order never enters the resting order book. Any unfilled remainder is cancelled.
    return tradeList;
}

trades Orderbook::matchFak(orderptr order) {
    trades tradeList;
    if (order->getside() == Side::Buy) {
        while (order->getrem() > 0 && !asks_.empty()) {
            auto bestAskIt = asks_.begin();
            int askPrice = bestAskIt->first;
            if (order->getprice() < askPrice) {
                break; // Limit price does not cross best ask
            }
            auto& askList = bestAskIt->second;

            while (order->getrem() > 0 && !askList.empty()) {
                auto restingAsk = askList.front();
                int matchQty = std::min(order->getrem(), restingAsk->getrem());
                int execPrice = restingAsk->getprice();

                order->fill(matchQty);
                restingAsk->fill(matchQty);

                tradeList.emplace_back(order->getorderid(), restingAsk->getorderid(), execPrice, matchQty);

                if (restingAsk->isfilled()) {
                    orders_.erase(restingAsk->getorderid());
                    askList.pop_front();
                }
            }

            if (askList.empty()) {
                asks_.erase(bestAskIt);
            }
        }
    } else { // FAK Sell
        while (order->getrem() > 0 && !bids_.empty()) {
            auto bestBidIt = bids_.begin();
            int bidPrice = bestBidIt->first;
            if (order->getprice() > bidPrice) {
                break; // Limit price does not cross best bid
            }
            auto& bidList = bestBidIt->second;

            while (order->getrem() > 0 && !bidList.empty()) {
                auto restingBid = bidList.front();
                int matchQty = std::min(order->getrem(), restingBid->getrem());
                int execPrice = restingBid->getprice();

                order->fill(matchQty);
                restingBid->fill(matchQty);

                tradeList.emplace_back(restingBid->getorderid(), order->getorderid(), execPrice, matchQty);

                if (restingBid->isfilled()) {
                    orders_.erase(restingBid->getorderid());
                    bidList.pop_front();
                }
            }

            if (bidList.empty()) {
                bids_.erase(bestBidIt);
            }
        }
    }
    // FAK (Fill-and-Kill / IOC): Remainder is immediately cancelled, never rests.
    return tradeList;
}

trades Orderbook::matchLimit(orderptr order) {
    trades tradeList;
    if (order->getside() == Side::Buy) {
        while (order->getrem() > 0 && !asks_.empty()) {
            auto bestAskIt = asks_.begin();
            int askPrice = bestAskIt->first;
            if (order->getprice() < askPrice) {
                break; // Cannot cross resting asks at this limit price
            }
            auto& askList = bestAskIt->second;

            while (order->getrem() > 0 && !askList.empty()) {
                auto restingAsk = askList.front();
                int matchQty = std::min(order->getrem(), restingAsk->getrem());
                int execPrice = restingAsk->getprice();

                order->fill(matchQty);
                restingAsk->fill(matchQty);

                tradeList.emplace_back(order->getorderid(), restingAsk->getorderid(), execPrice, matchQty);

                if (restingAsk->isfilled()) {
                    orders_.erase(restingAsk->getorderid());
                    askList.pop_front();
                }
            }

            if (askList.empty()) {
                asks_.erase(bestAskIt);
            }
        }

        // Limit order remaining quantity rests in the bids book
        if (order->getrem() > 0) {
            auto& list = bids_[order->getprice()];
            list.push_back(order);
            orders_[order->getorderid()] = {order, std::prev(list.end())};
        }
    } else { // Limit Sell
        while (order->getrem() > 0 && !bids_.empty()) {
            auto bestBidIt = bids_.begin();
            int bidPrice = bestBidIt->first;
            if (order->getprice() > bidPrice) {
                break; // Cannot cross resting bids at this limit price
            }
            auto& bidList = bestBidIt->second;

            while (order->getrem() > 0 && !bidList.empty()) {
                auto restingBid = bidList.front();
                int matchQty = std::min(order->getrem(), restingBid->getrem());
                int execPrice = restingBid->getprice();

                order->fill(matchQty);
                restingBid->fill(matchQty);

                tradeList.emplace_back(restingBid->getorderid(), order->getorderid(), execPrice, matchQty);

                if (restingBid->isfilled()) {
                    orders_.erase(restingBid->getorderid());
                    bidList.pop_front();
                }
            }

            if (bidList.empty()) {
                bids_.erase(bestBidIt);
            }
        }

        // Limit order remaining quantity rests in the asks book
        if (order->getrem() > 0) {
            auto& list = asks_[order->getprice()];
            list.push_back(order);
            orders_[order->getorderid()] = {order, std::prev(list.end())};
        }
    }
    return tradeList;
}

/* -----------------------------------------------------
   Public API Implementation
   ----------------------------------------------------- */

trades Orderbook::addorder(orderptr order) {
    validateNewOrder(order);

    switch (order->getordertype()) {
        case OrderType::Market:
            return matchMarket(order);
        case OrderType::FillAndKill:
            return matchFak(order);
        case OrderType::Limit:
            return matchLimit(order);
        default:
            throw std::invalid_argument("Unknown order type");
    }
}

void Orderbook::cancelorder(int id) {
    auto it = orders_.find(id);
    if (it == orders_.end()) {
        throw std::out_of_range("Order ID not found for cancellation: " + std::to_string(id));
    }

    const auto& [order, listIt] = it->second;
    int price = order->getprice();

    if (order->getside() == Side::Buy) {
        auto priceIt = bids_.find(price);
        if (priceIt != bids_.end()) {
            priceIt->second.erase(listIt);
            if (priceIt->second.empty()) {
                bids_.erase(priceIt);
            }
        }
    } else {
        auto priceIt = asks_.find(price);
        if (priceIt != asks_.end()) {
            priceIt->second.erase(listIt);
            if (priceIt->second.empty()) {
                asks_.erase(priceIt);
            }
        }
    }
    orders_.erase(it);
}

trades Orderbook::Matchorder(const ordermodify& omod) {
    validateModify(omod);

    auto it = orders_.find(omod.getorderid());
    auto existingOrder = it->second.order_;
    int oldPrice = existingOrder->getprice();
    int oldRem = existingOrder->getrem();
    int newPrice = omod.getprice();
    int newQty = omod.getquantity();

    // Case 1: Same price
    if (newPrice == oldPrice) {
        if (newQty == oldRem) {
            return {}; // No-op
        }
        if (newQty < oldRem) {
            // Quantity reduction: PRESERVE time priority (in-place modification)
            existingOrder->setRemainingQuantity(newQty);
            return {};
        } else {
            // Quantity increase: LOSES time priority (re-queued at the back of the price level)
            if (existingOrder->getside() == Side::Buy) {
                auto& list = bids_[oldPrice];
                list.erase(it->second.location_);
                existingOrder->setRemainingQuantity(newQty);
                list.push_back(existingOrder);
                it->second.location_ = std::prev(list.end());
            } else {
                auto& list = asks_[oldPrice];
                list.erase(it->second.location_);
                existingOrder->setRemainingQuantity(newQty);
                list.push_back(existingOrder);
                it->second.location_ = std::prev(list.end());
            }
            return {};
        }
    }

    // Case 2: Price changes: order is removed and re-inserted as a new order at the new price
    OrderType oldType = existingOrder->getordertype();
    cancelorder(omod.getorderid());
    auto newOrder = std::make_shared<Order>(oldType, omod.getorderid(), omod.getside(), newPrice, newQty);
    return addorder(newOrder);
}

AggregatedOrderbook Orderbook::getorderinfo() const {
    Levelinfos bidinfo;
    Levelinfos askinfo;
    bidinfo.reserve(bids_.size());
    askinfo.reserve(asks_.size());

    for (const auto& [price, ordersAtPrice] : bids_) {
        int total = 0;
        for (const auto& o : ordersAtPrice) {
            total += o->getrem();
        }
        if (total > 0) {
            bidinfo.push_back(Levelinfo{price, total});
        }
    }

    for (const auto& [price, ordersAtPrice] : asks_) {
        int total = 0;
        for (const auto& o : ordersAtPrice) {
            total += o->getrem();
        }
        if (total > 0) {
            askinfo.push_back(Levelinfo{price, total});
        }
    }

    return AggregatedOrderbook(bidinfo, askinfo);
}

bool Orderbook::hasorder(int id) const {
    return orders_.find(id) != orders_.end();
}

orderptr Orderbook::getorder(int id) const {
    auto it = orders_.find(id);
    if (it != orders_.end()) {
        return it->second.order_;
    }
    return nullptr;
}

size_t Orderbook::ordercount() const {
    return orders_.size();
}

bool Orderbook::hasBidLevel(int price) const {
    return bids_.find(price) != bids_.end();
}

bool Orderbook::hasAskLevel(int price) const {
    return asks_.find(price) != asks_.end();
}

size_t Orderbook::bidLevelCount() const {
    return bids_.size();
}

size_t Orderbook::askLevelCount() const {
    return asks_.size();
}
