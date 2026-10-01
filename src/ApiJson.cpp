#include "ApiJson.h"

#include <cctype>
#include <sstream>

namespace {

std::string escapeJson(const std::string& value) {
    std::ostringstream out;
    for (char ch : value) {
        switch (ch) {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default: out << ch; break;
        }
    }
    return out.str();
}

std::optional<std::string> findRawValue(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    size_t keyPos = json.find(needle);
    if (keyPos == std::string::npos) {
        return std::nullopt;
    }
    size_t colon = json.find(':', keyPos + needle.size());
    if (colon == std::string::npos) {
        return std::nullopt;
    }
    size_t pos = colon + 1;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }
    if (pos >= json.size()) {
        return std::nullopt;
    }
    if (json[pos] == '"') {
        std::string value;
        bool escaped = false;
        for (size_t i = pos + 1; i < json.size(); ++i) {
            char ch = json[i];
            if (escaped) {
                value.push_back(ch);
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == '"') {
                return value;
            } else {
                value.push_back(ch);
            }
        }
        return std::nullopt;
    }

    size_t end = pos;
    while (end < json.size() && json[end] != ',' && json[end] != '}') {
        ++end;
    }
    while (end > pos && std::isspace(static_cast<unsigned char>(json[end - 1]))) {
        --end;
    }
    return json.substr(pos, end - pos);
}

std::optional<std::string> getString(const std::string& json, const std::string& key) {
    return findRawValue(json, key);
}

std::optional<int> getInt(const std::string& json, const std::string& key) {
    auto raw = findRawValue(json, key);
    if (!raw || raw->empty()) {
        return std::nullopt;
    }
    try {
        size_t consumed = 0;
        int value = std::stoi(*raw, &consumed);
        return consumed == raw->size() ? std::optional<int>(value) : std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<Side> parseSide(const std::string& value) {
    if (value == "BUY" || value == "Buy" || value == "buy") {
        return Side::Buy;
    }
    if (value == "SELL" || value == "Sell" || value == "sell") {
        return Side::Sell;
    }
    return std::nullopt;
}

std::optional<OrderType> parseOrderType(const std::string& value) {
    if (value == "LIMIT" || value == "Limit" || value == "limit") {
        return OrderType::Limit;
    }
    if (value == "MARKET" || value == "Market" || value == "market") {
        return OrderType::Market;
    }
    if (value == "FAK" || value == "IOC" || value == "FillAndKill" || value == "fillandkill") {
        return OrderType::FillAndKill;
    }
    return std::nullopt;
}

std::string serializeTradesArray(const std::vector<Trade>& trades) {
    std::ostringstream out;
    out << '[';
    for (size_t i = 0; i < trades.size(); ++i) {
        if (i > 0) {
            out << ',';
        }
        out << "{\"buyOrderId\":" << trades[i].getBuyOrderId()
            << ",\"sellOrderId\":" << trades[i].getSellOrderId()
            << ",\"price\":" << trades[i].getPrice()
            << ",\"quantity\":" << trades[i].getQuantity() << '}';
    }
    out << ']';
    return out.str();
}

} // namespace

namespace ApiJson {

std::optional<ApiNewOrderRequest> parseNewOrderRequest(const std::string& json, std::string& error) {
    ApiNewOrderRequest request;
    auto sideValue = getString(json, "side");
    auto typeValue = getString(json, "orderType");
    auto quantityValue = getInt(json, "quantity");

    if (!sideValue) {
        error = "side is required";
        return std::nullopt;
    }
    auto side = parseSide(*sideValue);
    if (!side) {
        error = "side must be BUY or SELL";
        return std::nullopt;
    }
    request.side = *side;

    if (!typeValue) {
        error = "orderType is required";
        return std::nullopt;
    }
    auto orderType = parseOrderType(*typeValue);
    if (!orderType) {
        error = "orderType must be LIMIT, MARKET, or FAK";
        return std::nullopt;
    }
    request.orderType = *orderType;

    if (!quantityValue) {
        error = "quantity is required";
        return std::nullopt;
    }
    request.quantity = *quantityValue;
    request.price = getInt(json, "price").value_or(0);
    request.clientOrderId = getString(json, "clientOrderId").value_or("");
    request.symbol = getString(json, "symbol").value_or("STOCK");
    return request;
}

std::optional<ApiCancelOrderRequest> parseCancelOrderRequest(const std::string& json, std::string& error) {
    ApiCancelOrderRequest request;
    request.orderId = getInt(json, "orderId").value_or(0);
    request.clientOrderId = getString(json, "clientOrderId").value_or("");
    if (auto sideValue = getString(json, "side")) {
        auto side = parseSide(*sideValue);
        if (!side) {
            error = "side must be BUY or SELL";
            return std::nullopt;
        }
        request.side = *side;
    }
    return request;
}

std::optional<ApiModifyOrderRequest> parseModifyOrderRequest(const std::string& json, std::string& error) {
    ApiModifyOrderRequest request;
    request.orderId = getInt(json, "orderId").value_or(0);
    request.clientOrderId = getString(json, "clientOrderId").value_or("");
    request.price = getInt(json, "price").value_or(0);
    auto quantity = getInt(json, "quantity");
    if (!quantity) {
        error = "quantity is required";
        return std::nullopt;
    }
    request.quantity = *quantity;
    if (auto sideValue = getString(json, "side")) {
        auto side = parseSide(*sideValue);
        if (!side) {
            error = "side must be BUY or SELL";
            return std::nullopt;
        }
        request.side = *side;
    }
    return request;
}

std::string serializeResponse(const ApiResponse& response) {
    std::ostringstream out;
    out << "{\"success\":" << (response.success ? "true" : "false")
        << ",\"httpStatus\":" << response.httpStatus
        << ",\"message\":\"" << escapeJson(response.message) << "\""
        << ",\"sequenceNumber\":" << response.sequenceNumber
        << ",\"orderId\":" << response.orderId
        << ",\"trades\":" << serializeTradesArray(response.executedTrades) << '}';
    return out.str();
}

std::string serializeOrderStatus(const ApiOrderStatus& status) {
    std::ostringstream out;
    out << "{\"orderId\":" << status.orderId
        << ",\"clientOrderId\":\"" << escapeJson(status.clientOrderId) << "\""
        << ",\"symbol\":\"" << escapeJson(status.symbol) << "\""
        << ",\"side\":\"" << toString(status.side) << "\""
        << ",\"orderType\":\"" << toString(status.orderType) << "\""
        << ",\"price\":" << status.price
        << ",\"originalQuantity\":" << status.originalQuantity
        << ",\"filledQuantity\":" << status.filledQuantity
        << ",\"remainingQuantity\":" << status.remainingQuantity
        << ",\"status\":\"" << toString(status.status) << "\"";
    if (!status.rejectionReason.empty()) {
        out << ",\"rejectionReason\":\"" << escapeJson(status.rejectionReason) << "\"";
    }
    out << '}';
    return out.str();
}

std::string serializeOrderStatuses(const std::vector<ApiOrderStatus>& statuses) {
    std::ostringstream out;
    out << '[';
    for (size_t i = 0; i < statuses.size(); ++i) {
        if (i > 0) {
            out << ',';
        }
        out << serializeOrderStatus(statuses[i]);
    }
    out << ']';
    return out.str();
}

std::string serializeOrderbook(const ApiOrderbookResponse& orderbook) {
    std::ostringstream out;
    out << "{\"sequenceNumber\":" << orderbook.sequenceNumber << ",\"bids\":[";
    for (size_t i = 0; i < orderbook.bids.size(); ++i) {
        if (i > 0) {
            out << ',';
        }
        out << "{\"price\":" << orderbook.bids[i].price << ",\"quantity\":" << orderbook.bids[i].quantity << '}';
    }
    out << "],\"asks\":[";
    for (size_t i = 0; i < orderbook.asks.size(); ++i) {
        if (i > 0) {
            out << ',';
        }
        out << "{\"price\":" << orderbook.asks[i].price << ",\"quantity\":" << orderbook.asks[i].quantity << '}';
    }
    out << "]}";
    return out.str();
}

std::string serializeTrades(const std::vector<ApiTradeRecord>& trades) {
    std::ostringstream out;
    out << '[';
    for (size_t i = 0; i < trades.size(); ++i) {
        if (i > 0) {
            out << ',';
        }
        out << "{\"tradeId\":" << trades[i].tradeId
            << ",\"buyOrderId\":" << trades[i].buyOrderId
            << ",\"sellOrderId\":" << trades[i].sellOrderId
            << ",\"price\":" << trades[i].price
            << ",\"quantity\":" << trades[i].quantity
            << ",\"timestampNanos\":" << trades[i].timestampNanos << '}';
    }
    out << ']';
    return out.str();
}

std::string serializeError(int status, const std::string& message) {
    std::ostringstream out;
    out << "{\"success\":false,\"httpStatus\":" << status
        << ",\"message\":\"" << escapeJson(message) << "\"}";
    return out.str();
}

std::string serializeHealth(bool engineRunning) {
    std::ostringstream out;
    out << "{\"status\":\"ok\",\"engineRunning\":" << (engineRunning ? "true" : "false") << '}';
    return out.str();
}

} // namespace ApiJson
