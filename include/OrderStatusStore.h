#ifndef ORDERSTATUSSTORE_H
#define ORDERSTATUSSTORE_H

#include "ApiModels.h"
#include "OrderEvent.h"

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class OrderStatusStore {
public:
    void registerNewOrder(int orderId, const ApiNewOrderRequest& request);
    void registerReplace(int orderId, const ApiModifyOrderRequest& request);
    void restore(const ApiOrderStatus& status);
    void onEvent(const OrderEventResult& result);
    void onTrade(const Trade& trade);

    std::optional<ApiOrderStatus> getByOrderId(int orderId) const;
    std::optional<ApiOrderStatus> getByClientOrderId(const std::string& clientOrderId) const;
    std::vector<ApiOrderStatus> allOrders() const;

private:
    void applyFillLocked(int orderId, int quantity);

    mutable std::mutex mutex_;
    std::unordered_map<int, ApiOrderStatus> byOrderId_;
    std::unordered_map<std::string, int> clientToOrderId_;
};

#endif // ORDERSTATUSSTORE_H
