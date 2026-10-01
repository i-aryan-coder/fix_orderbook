#ifndef APIMODELS_H
#define APIMODELS_H

#include "Order.h"

#include <cstdint>
#include <string>
#include <vector>

struct ApiNewOrderRequest {
    std::string clientOrderId;
    std::string symbol{"STOCK"};
    Side side{Side::Buy};
    OrderType orderType{OrderType::Limit};
    int price{0};
    int quantity{0};
};

struct ApiCancelOrderRequest {
    int orderId{0};
    std::string clientOrderId;
    Side side{Side::Buy};
};

struct ApiModifyOrderRequest {
    int orderId{0};
    std::string clientOrderId;
    Side side{Side::Buy};
    int price{0};
    int quantity{0};
};

enum class ApiOrderStatusType {
    New,
    PartiallyFilled,
    Filled,
    Canceled,
    Replaced,
    Rejected
};

struct ApiOrderStatus {
    int orderId{0};
    std::string clientOrderId;
    std::string symbol{"STOCK"};
    Side side{Side::Buy};
    OrderType orderType{OrderType::Limit};
    int price{0};
    int originalQuantity{0};
    int filledQuantity{0};
    int remainingQuantity{0};
    ApiOrderStatusType status{ApiOrderStatusType::New};
    std::string rejectionReason;
};

struct ApiTradeRecord {
    uint64_t tradeId{0};
    int buyOrderId{0};
    int sellOrderId{0};
    int price{0};
    int quantity{0};
    uint64_t timestampNanos{0};
};

struct ApiResponse {
    bool success{false};
    int httpStatus{200};
    std::string message;
    uint64_t sequenceNumber{0};
    int orderId{0};
    std::vector<Trade> executedTrades;
};

struct ApiOrderbookResponse {
    uint64_t sequenceNumber{0};
    Levelinfos bids;
    Levelinfos asks;
};

const char* toString(Side side);
const char* toString(OrderType orderType);
const char* toString(ApiOrderStatusType status);

#endif // APIMODELS_H
