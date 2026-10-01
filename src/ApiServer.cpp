#include "ApiServer.h"

#include "ApiJson.h"

#include <sstream>
#include <utility>

ApiServer::ApiServer(MatchingEngine& engine, size_t tradeCapacity, size_t outboundCapacity)
    : engine_(engine), tradeStore_(tradeCapacity), outboundQueue_(outboundCapacity) {
    engine_.addTradeListener([this](const Trade& trade) {
        ApiTradeRecord record = tradeStore_.recordTrade(trade);
        orderStatusStore_.onTrade(trade);
        outboundQueue_.tryPush(OutboundMessage{OutboundMessage::Type::Trade, tradePayload(record)});
    });
    engine_.addEventListener([this](const OrderEventResult& result) {
        orderStatusStore_.onEvent(result);
        enqueueOrderUpdate(result);
        enqueueOrderbookUpdate();
    });
}

ApiResponse ApiServer::submitOrder(const ApiNewOrderRequest& request) {
    auto validationError = validateNewOrder(request);
    if (validationError) {
        return reject(400, *validationError);
    }

    int orderId = nextRestOrderId();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!request.clientOrderId.empty() && clientOrderIds_.find(request.clientOrderId) != clientOrderIds_.end()) {
            return reject(409, "Duplicate clientOrderId");
        }
        if (!request.clientOrderId.empty()) {
            clientOrderIds_[request.clientOrderId] = orderId;
        }
    }
    orderStatusStore_.registerNewOrder(orderId, request);

    auto future = engine_.submitNewOrderSync(makeOrder(orderId, request));
    OrderEventResult result = future.get();
    ApiResponse response;
    response.success = result.success;
    response.httpStatus = result.success ? 201 : 400;
    response.message = result.success ? "Order accepted" : result.errorMessage;
    response.sequenceNumber = result.sequenceNumber;
    response.orderId = orderId;
    response.executedTrades = result.executedTrades;
    return response;
}

ApiResponse ApiServer::cancelOrder(const ApiCancelOrderRequest& request) {
    auto validationError = validateCancel(request);
    if (validationError) {
        return reject(400, *validationError);
    }

    int orderId = resolveOrderId(request.orderId, request.clientOrderId);
    if (orderId == 0) {
        return reject(404, "Unknown order");
    }

    auto future = engine_.submitCancelOrderSync(orderId);
    OrderEventResult result = future.get();
    ApiResponse response;
    response.success = result.success;
    response.httpStatus = result.success ? 200 : 404;
    response.message = result.success ? "Order canceled" : result.errorMessage;
    response.sequenceNumber = result.sequenceNumber;
    response.orderId = orderId;
    response.executedTrades = result.executedTrades;
    return response;
}

ApiResponse ApiServer::modifyOrder(const ApiModifyOrderRequest& request) {
    auto validationError = validateModify(request);
    if (validationError) {
        return reject(400, *validationError);
    }

    int orderId = resolveOrderId(request.orderId, request.clientOrderId);
    if (orderId == 0) {
        return reject(404, "Unknown order");
    }

    auto previousStatus = orderStatusStore_.getByOrderId(orderId);
    orderStatusStore_.registerReplace(orderId, request);
    auto future = engine_.submitModifyOrderSync(OrderModify(orderId, request.side, request.price, request.quantity));
    OrderEventResult result = future.get();
    if (!result.success && previousStatus) {
        orderStatusStore_.restore(*previousStatus);
    }
    if (result.success && !request.clientOrderId.empty()) {
        std::lock_guard<std::mutex> lock(mutex_);
        clientOrderIds_.erase(previousStatus ? previousStatus->clientOrderId : std::string{});
        clientOrderIds_[request.clientOrderId] = orderId;
    }

    ApiResponse response;
    response.success = result.success;
    response.httpStatus = result.success ? 200 : 400;
    response.message = result.success ? "Order modified" : result.errorMessage;
    response.sequenceNumber = result.sequenceNumber;
    response.orderId = orderId;
    response.executedTrades = result.executedTrades;
    return response;
}

std::optional<ApiOrderStatus> ApiServer::getOrderStatus(int orderId) const {
    return orderStatusStore_.getByOrderId(orderId);
}

std::optional<ApiOrderStatus> ApiServer::getOrderStatus(const std::string& clientOrderId) const {
    return orderStatusStore_.getByClientOrderId(clientOrderId);
}

std::vector<ApiOrderStatus> ApiServer::getAllOrders() const {
    return orderStatusStore_.allOrders();
}

ApiOrderbookResponse ApiServer::getOrderbook() const {
    auto snapshot = engine_.getSnapshot();
    ApiOrderbookResponse response;
    response.sequenceNumber = snapshot->getSequenceNumber();
    response.bids = snapshot->getbids();
    response.asks = snapshot->getasks();
    return response;
}

std::vector<ApiTradeRecord> ApiServer::getRecentTrades() const {
    return tradeStore_.recentTrades();
}

bool ApiServer::isEngineRunning() const {
    return engine_.isRunning();
}

OutboundBroadcastQueue& ApiServer::outboundQueue() {
    return outboundQueue_;
}

size_t ApiServer::droppedBroadcastCount() const {
    return outboundQueue_.droppedCount();
}

ApiResponse ApiServer::reject(int httpStatus, const std::string& message) const {
    ApiResponse response;
    response.success = false;
    response.httpStatus = httpStatus;
    response.message = message;
    return response;
}

std::optional<std::string> ApiServer::validateNewOrder(const ApiNewOrderRequest& request) const {
    if (request.side != Side::Buy && request.side != Side::Sell) {
        return "side must be BUY or SELL";
    }
    if (request.quantity <= 0) {
        return "quantity must be positive";
    }
    if (request.orderType != OrderType::Limit && request.orderType != OrderType::Market &&
        request.orderType != OrderType::FillAndKill) {
        return "unsupported order type";
    }
    if (request.orderType != OrderType::Market && request.price <= 0) {
        return "price must be positive for LIMIT and FAK orders";
    }
    if (request.orderType == OrderType::Market && request.price != 0) {
        return "market orders must not include a price";
    }
    return std::nullopt;
}

std::optional<std::string> ApiServer::validateCancel(const ApiCancelOrderRequest& request) const {
    if (request.side != Side::Buy && request.side != Side::Sell) {
        return "side must be BUY or SELL";
    }
    if (request.orderId <= 0 && request.clientOrderId.empty()) {
        return "orderId or clientOrderId is required";
    }
    return std::nullopt;
}

std::optional<std::string> ApiServer::validateModify(const ApiModifyOrderRequest& request) const {
    if (request.side != Side::Buy && request.side != Side::Sell) {
        return "side must be BUY or SELL";
    }
    if (request.orderId <= 0 && request.clientOrderId.empty()) {
        return "orderId or clientOrderId is required";
    }
    if (request.quantity <= 0) {
        return "quantity must be positive";
    }
    if (request.price <= 0) {
        return "price must be positive";
    }
    return std::nullopt;
}

int ApiServer::resolveOrderId(int orderId, const std::string& clientOrderId) const {
    if (orderId > 0) {
        return orderId;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = clientOrderIds_.find(clientOrderId);
    if (it == clientOrderIds_.end()) {
        return 0;
    }
    return it->second;
}

int ApiServer::nextRestOrderId() {
    return nextOrderId_.fetch_add(1);
}

orderptr ApiServer::makeOrder(int orderId, const ApiNewOrderRequest& request) const {
    if (request.orderType == OrderType::Market) {
        return std::make_shared<Order>(orderId, request.side, request.quantity);
    }
    return std::make_shared<Order>(request.orderType, orderId, request.side, request.price, request.quantity);
}

void ApiServer::enqueueOrderbookUpdate() {
    auto snapshot = engine_.getSnapshot();
    outboundQueue_.tryPush(OutboundMessage{OutboundMessage::Type::Orderbook, orderbookPayload(*snapshot)});
}

void ApiServer::enqueueOrderUpdate(const OrderEventResult& result) {
    outboundQueue_.tryPush(OutboundMessage{OutboundMessage::Type::OrderUpdate, orderEventPayload(result)});
}

std::string ApiServer::tradePayload(const ApiTradeRecord& trade) {
    std::ostringstream out;
    out << "{\"type\":\"trade\""
        << ",\"tradeId\":" << trade.tradeId
        << ",\"buyOrderId\":" << trade.buyOrderId
        << ",\"sellOrderId\":" << trade.sellOrderId
        << ",\"price\":" << trade.price
        << ",\"quantity\":" << trade.quantity
        << ",\"timestampNanos\":" << trade.timestampNanos << "}";
    return out.str();
}

std::string ApiServer::orderbookPayload(const OrderbookSnapshot& snapshot) {
    ApiOrderbookResponse response;
    response.sequenceNumber = snapshot.getSequenceNumber();
    response.bids = snapshot.getbids();
    response.asks = snapshot.getasks();
    std::string payload = ApiJson::serializeOrderbook(response);
    payload.insert(1, "\"type\":\"orderbook\",");
    return payload;
}

std::string ApiServer::orderEventPayload(const OrderEventResult& result) {
    const char* eventType = "UNKNOWN";
    switch (result.eventType) {
        case EventType::NewOrder: eventType = "NEW"; break;
        case EventType::CancelOrder: eventType = "CANCEL"; break;
        case EventType::ModifyOrder: eventType = "MODIFY"; break;
        case EventType::Shutdown: eventType = "SHUTDOWN"; break;
    }

    std::ostringstream out;
    out << "{\"type\":\"orderUpdate\""
        << ",\"sequenceNumber\":" << result.sequenceNumber
        << ",\"eventType\":\"" << eventType << "\""
        << ",\"orderId\":" << result.orderId
        << ",\"success\":" << (result.success ? "true" : "false");
    if (!result.errorMessage.empty()) {
        out << ",\"message\":\"" << result.errorMessage << "\"";
    }
    out << "}";
    return out.str();
}
