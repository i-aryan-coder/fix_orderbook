#ifndef APISERVER_H
#define APISERVER_H

#include "ApiModels.h"
#include "MatchingEngine.h"
#include "OrderStatusStore.h"
#include "OutboundBroadcastQueue.h"
#include "TradeStore.h"

#include <atomic>
#include <optional>
#include <string>
#include <unordered_map>
#include <mutex>

class ApiServer {
public:
    explicit ApiServer(MatchingEngine& engine, size_t tradeCapacity = 1000, size_t outboundCapacity = 1024);

    ApiResponse submitOrder(const ApiNewOrderRequest& request);
    ApiResponse cancelOrder(const ApiCancelOrderRequest& request);
    ApiResponse modifyOrder(const ApiModifyOrderRequest& request);

    std::optional<ApiOrderStatus> getOrderStatus(int orderId) const;
    std::optional<ApiOrderStatus> getOrderStatus(const std::string& clientOrderId) const;
    std::vector<ApiOrderStatus> getAllOrders() const;
    ApiOrderbookResponse getOrderbook() const;
    std::vector<ApiTradeRecord> getRecentTrades() const;
    bool isEngineRunning() const;

    OutboundBroadcastQueue& outboundQueue();
    size_t droppedBroadcastCount() const;

private:
    ApiResponse reject(int httpStatus, const std::string& message) const;
    std::optional<std::string> validateNewOrder(const ApiNewOrderRequest& request) const;
    std::optional<std::string> validateCancel(const ApiCancelOrderRequest& request) const;
    std::optional<std::string> validateModify(const ApiModifyOrderRequest& request) const;
    int resolveOrderId(int orderId, const std::string& clientOrderId) const;
    int nextRestOrderId();
    orderptr makeOrder(int orderId, const ApiNewOrderRequest& request) const;
    void enqueueOrderbookUpdate();
    void enqueueOrderUpdate(const OrderEventResult& result);
    static std::string tradePayload(const ApiTradeRecord& trade);
    static std::string orderbookPayload(const OrderbookSnapshot& snapshot);
    static std::string orderEventPayload(const OrderEventResult& result);

    MatchingEngine& engine_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, int> clientOrderIds_;
    std::atomic<int> nextOrderId_{1000000};
    OrderStatusStore orderStatusStore_;
    TradeStore tradeStore_;
    OutboundBroadcastQueue outboundQueue_;
};

#endif // APISERVER_H
