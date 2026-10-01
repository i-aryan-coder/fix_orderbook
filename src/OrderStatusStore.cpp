#include "OrderStatusStore.h"

#include <algorithm>

void OrderStatusStore::registerNewOrder(int orderId, const ApiNewOrderRequest& request) {
    std::lock_guard<std::mutex> lock(mutex_);
    ApiOrderStatus status;
    status.orderId = orderId;
    status.clientOrderId = request.clientOrderId;
    status.symbol = request.symbol;
    status.side = request.side;
    status.orderType = request.orderType;
    status.price = request.price;
    status.originalQuantity = request.quantity;
    status.remainingQuantity = request.quantity;
    status.status = ApiOrderStatusType::New;
    byOrderId_[orderId] = status;
    if (!request.clientOrderId.empty()) {
        clientToOrderId_[request.clientOrderId] = orderId;
    }
}

void OrderStatusStore::registerReplace(int orderId, const ApiModifyOrderRequest& request) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = byOrderId_.find(orderId);
    if (it == byOrderId_.end()) {
        return;
    }
    if (!request.clientOrderId.empty()) {
        clientToOrderId_.erase(it->second.clientOrderId);
        it->second.clientOrderId = request.clientOrderId;
        clientToOrderId_[request.clientOrderId] = orderId;
    }
    it->second.price = request.price;
    it->second.originalQuantity = it->second.filledQuantity + request.quantity;
    it->second.remainingQuantity = request.quantity;
    it->second.status = ApiOrderStatusType::Replaced;
}

void OrderStatusStore::restore(const ApiOrderStatus& status) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto existing = byOrderId_.find(status.orderId);
    if (existing != byOrderId_.end() && !existing->second.clientOrderId.empty()) {
        clientToOrderId_.erase(existing->second.clientOrderId);
    }
    byOrderId_[status.orderId] = status;
    if (!status.clientOrderId.empty()) {
        clientToOrderId_[status.clientOrderId] = status.orderId;
    }
}

void OrderStatusStore::onEvent(const OrderEventResult& result) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = byOrderId_.find(result.orderId);
    if (it == byOrderId_.end()) {
        return;
    }

    ApiOrderStatus& status = it->second;
    if (!result.success) {
        status.status = ApiOrderStatusType::Rejected;
        status.rejectionReason = result.errorMessage;
        return;
    }

    if (result.eventType == EventType::CancelOrder) {
        status.status = ApiOrderStatusType::Canceled;
        status.remainingQuantity = 0;
        return;
    }

    if (result.eventType == EventType::NewOrder &&
        (status.orderType == OrderType::Market || status.orderType == OrderType::FillAndKill) &&
        status.remainingQuantity > 0) {
        status.status = status.filledQuantity > 0 ? ApiOrderStatusType::Canceled : ApiOrderStatusType::Canceled;
        status.remainingQuantity = 0;
    }
}

void OrderStatusStore::onTrade(const Trade& trade) {
    std::lock_guard<std::mutex> lock(mutex_);
    applyFillLocked(trade.getBuyOrderId(), trade.getQuantity());
    applyFillLocked(trade.getSellOrderId(), trade.getQuantity());
}

std::optional<ApiOrderStatus> OrderStatusStore::getByOrderId(int orderId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = byOrderId_.find(orderId);
    if (it == byOrderId_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<ApiOrderStatus> OrderStatusStore::getByClientOrderId(const std::string& clientOrderId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto mapIt = clientToOrderId_.find(clientOrderId);
    if (mapIt == clientToOrderId_.end()) {
        return std::nullopt;
    }
    auto statusIt = byOrderId_.find(mapIt->second);
    if (statusIt == byOrderId_.end()) {
        return std::nullopt;
    }
    return statusIt->second;
}

std::vector<ApiOrderStatus> OrderStatusStore::allOrders() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ApiOrderStatus> orders;
    orders.reserve(byOrderId_.size());
    for (const auto& item : byOrderId_) {
        orders.push_back(item.second);
    }
    std::sort(orders.begin(), orders.end(), [](const ApiOrderStatus& lhs, const ApiOrderStatus& rhs) {
        return lhs.orderId < rhs.orderId;
    });
    return orders;
}

void OrderStatusStore::applyFillLocked(int orderId, int quantity) {
    auto it = byOrderId_.find(orderId);
    if (it == byOrderId_.end()) {
        return;
    }
    ApiOrderStatus& status = it->second;
    status.filledQuantity += quantity;
    status.remainingQuantity = std::max(0, status.remainingQuantity - quantity);
    status.status = status.remainingQuantity == 0
        ? ApiOrderStatusType::Filled
        : ApiOrderStatusType::PartiallyFilled;
}
