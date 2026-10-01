#ifndef ORDEREVENT_H
#define ORDEREVENT_H

#include "Order.h"
#include <cstdint>
#include <string>
#include <memory>
#include <future>

/**
 * @brief Types of events ingested by the Matching Engine.
 */
enum class EventType {
    NewOrder,
    CancelOrder,
    ModifyOrder,
    Shutdown
};

/**
 * @brief Result of processing an order event by the matching thread.
 */
struct OrderEventResult {
    uint64_t sequenceNumber{0};
    EventType eventType{EventType::NewOrder};
    int orderId{0};
    bool success{true};
    std::string errorMessage{};
    trades executedTrades{};
};

/**
 * @brief Internal order event transferred from producers to the dedicated matching thread.
 */
struct OrderEvent {
    uint64_t sequenceNumber{0};
    EventType type{EventType::NewOrder};
    orderptr order{nullptr};
    int orderId{0};
    OrderModify modifyReq{0, Side::Buy, 1, 1};
    std::shared_ptr<std::promise<OrderEventResult>> promiseResult{nullptr};

    // Constructors for convenience
    static OrderEvent makeNewOrder(uint64_t seq, orderptr o, std::shared_ptr<std::promise<OrderEventResult>> prom = nullptr) {
        OrderEvent ev;
        ev.sequenceNumber = seq;
        ev.type = EventType::NewOrder;
        ev.order = std::move(o);
        if (ev.order) {
            ev.orderId = ev.order->getorderid();
        }
        ev.promiseResult = std::move(prom);
        return ev;
    }

    static OrderEvent makeCancelOrder(uint64_t seq, int id, std::shared_ptr<std::promise<OrderEventResult>> prom = nullptr) {
        OrderEvent ev;
        ev.sequenceNumber = seq;
        ev.type = EventType::CancelOrder;
        ev.orderId = id;
        ev.promiseResult = std::move(prom);
        return ev;
    }

    static OrderEvent makeModifyOrder(uint64_t seq, const OrderModify& mod, std::shared_ptr<std::promise<OrderEventResult>> prom = nullptr) {
        OrderEvent ev;
        ev.sequenceNumber = seq;
        ev.type = EventType::ModifyOrder;
        ev.orderId = mod.getorderid();
        ev.modifyReq = mod;
        ev.promiseResult = std::move(prom);
        return ev;
    }

    static OrderEvent makeShutdown(uint64_t seq) {
        OrderEvent ev;
        ev.sequenceNumber = seq;
        ev.type = EventType::Shutdown;
        return ev;
    }
};

#endif // ORDEREVENT_H
