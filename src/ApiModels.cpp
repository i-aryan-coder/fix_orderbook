#include "ApiModels.h"

const char* toString(Side side) {
    return side == Side::Buy ? "BUY" : "SELL";
}

const char* toString(OrderType orderType) {
    switch (orderType) {
        case OrderType::Limit: return "LIMIT";
        case OrderType::Market: return "MARKET";
        case OrderType::FillAndKill: return "FAK";
    }
    return "UNKNOWN";
}

const char* toString(ApiOrderStatusType status) {
    switch (status) {
        case ApiOrderStatusType::New: return "NEW";
        case ApiOrderStatusType::PartiallyFilled: return "PARTIALLY_FILLED";
        case ApiOrderStatusType::Filled: return "FILLED";
        case ApiOrderStatusType::Canceled: return "CANCELED";
        case ApiOrderStatusType::Replaced: return "REPLACED";
        case ApiOrderStatusType::Rejected: return "REJECTED";
    }
    return "UNKNOWN";
}
