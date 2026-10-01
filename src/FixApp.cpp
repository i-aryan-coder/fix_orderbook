#include "FixApp.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <utility>

const char* toFixExecType(FixExecType type) {
    switch (type) {
        case FixExecType::New: return "0";
        case FixExecType::PartialFill: return "1";
        case FixExecType::Fill: return "2";
        case FixExecType::Canceled: return "4";
        case FixExecType::Replace: return "5";
        case FixExecType::Rejected: return "8";
    }
    return "8";
}

const char* toFixOrdStatus(FixOrdStatus status) {
    switch (status) {
        case FixOrdStatus::New: return "0";
        case FixOrdStatus::PartiallyFilled: return "1";
        case FixOrdStatus::Filled: return "2";
        case FixOrdStatus::Canceled: return "4";
        case FixOrdStatus::Replaced: return "5";
        case FixOrdStatus::Rejected: return "8";
    }
    return "8";
}

FixApp::FixApp(MatchingEngine& engine) : engine_(engine) {
    engine_.addTradeListener([this](const Trade& trade) {
        onTrade(trade);
    });
    engine_.addEventListener([this](const OrderEventResult& result) {
        onOrderEvent(result);
    });
}

uint64_t FixApp::nextInternalOrderId() {
    return static_cast<uint64_t>(nextOrderId_.fetch_add(1));
}

std::string FixApp::nextExecId() {
    return "E" + std::to_string(nextExecId_.fetch_add(1));
}

std::shared_ptr<Order> FixApp::makeOrder(int internalId, const FixNewOrderRequest& request) const {
    if (request.orderType == OrderType::Market) {
        return std::make_shared<Order>(internalId, request.side, request.orderQty);
    }
    return std::make_shared<Order>(request.orderType, internalId, request.side, request.price, request.orderQty);
}

void FixApp::addReportListener(ReportListener listener) {
    std::lock_guard<std::mutex> lock(mutex_);
    reportListeners_.push_back(std::move(listener));
}

std::vector<FixExecutionReport> FixApp::getReports() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return reports_;
}

std::optional<int> FixApp::getInternalOrderId(const std::string& clOrdId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = clOrdIdToInternalId_.find(clOrdId);
    if (it == clOrdIdToInternalId_.end()) {
        return std::nullopt;
    }
    return it->second;
}

FixExecutionReport FixApp::makeBaseReport(const OrderState& state, FixExecType execType, FixOrdStatus ordStatus) const {
    FixExecutionReport report;
    report.clOrdId = state.activeClOrdId;
    report.orderId = std::to_string(state.internalOrderId);
    report.execType = execType;
    report.ordStatus = ordStatus;
    report.symbol = state.symbol;
    report.side = state.side;
    report.orderQty = state.orderQty;
    report.cumQty = state.cumQty;
    report.leavesQty = state.leavesQty;
    report.avgPx = state.cumQty == 0 ? 0.0 : static_cast<double>(state.notional) / state.cumQty;
    return report;
}

void FixApp::emitReport(const FixExecutionReport& report) {
    std::vector<ReportListener> listeners;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        reports_.push_back(report);
        listeners = reportListeners_;
    }

#if FIXAPP_HAS_QUICKFIX
    sendQuickFixReport(report);
#endif

    for (const auto& listener : listeners) {
        listener(report);
    }
}

void FixApp::reject(const std::string& clOrdId, const std::string& origClOrdId, const std::string& symbol,
                    Side side, const std::string& reason) {
    FixExecutionReport report;
    report.clOrdId = clOrdId;
    report.origClOrdId = origClOrdId;
    report.orderId = "NONE";
    report.execId = nextExecId();
    report.execType = FixExecType::Rejected;
    report.ordStatus = FixOrdStatus::Rejected;
    report.symbol = symbol.empty() ? "STOCK" : symbol;
    report.side = side;
    report.text = reason;
    emitReport(report);
}

uint64_t FixApp::submitNewOrder(const FixNewOrderRequest& request) {
    if (request.clOrdId.empty()) {
        reject(request.clOrdId, {}, request.symbol, request.side, "ClOrdID is required");
        return 0;
    }
    if (request.orderQty <= 0) {
        reject(request.clOrdId, {}, request.symbol, request.side, "OrderQty must be positive");
        return 0;
    }
    if (request.orderType != OrderType::Limit && request.orderType != OrderType::Market &&
        request.orderType != OrderType::FillAndKill) {
        reject(request.clOrdId, {}, request.symbol, request.side, "Unsupported order type");
        return 0;
    }
    if (request.orderType != OrderType::Market && request.price <= 0) {
        reject(request.clOrdId, {}, request.symbol, request.side, "Price must be positive for Limit and FAK orders");
        return 0;
    }

    int internalId = static_cast<int>(nextInternalOrderId());
    bool duplicate = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        duplicate = seenClOrdIds_.find(request.clOrdId) != seenClOrdIds_.end();
        if (!duplicate) {
            seenClOrdIds_.insert(request.clOrdId);
            clOrdIdToInternalId_[request.clOrdId] = internalId;
            ordersByInternalId_[internalId] = OrderState{internalId, request.clOrdId, request.symbol,
                                                         request.side, request.orderType, request.orderQty,
                                                         0, request.orderQty, 0, false};
        }
    }

    if (duplicate) {
        reject(request.clOrdId, {}, request.symbol, request.side, "Duplicate ClOrdID");
        return 0;
    }

    FixExecutionReport ack;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ack = makeBaseReport(ordersByInternalId_.at(internalId), FixExecType::New, FixOrdStatus::New);
    }
    ack.execId = nextExecId();
    emitReport(ack);

    auto order = makeOrder(internalId, request);
    uint64_t seq = engine_.submitNewOrder(order);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingClientIdsBySequence_[seq] = {request.clOrdId, {}};
    }
    return seq;
}

uint64_t FixApp::submitCancel(const FixCancelRequest& request) {
    if (request.clOrdId.empty() || request.origClOrdId.empty()) {
        reject(request.clOrdId, request.origClOrdId, request.symbol, request.side, "ClOrdID and OrigClOrdID are required");
        return 0;
    }

    int internalId = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (seenClOrdIds_.find(request.clOrdId) != seenClOrdIds_.end()) {
            internalId = 0;
        } else {
            auto it = clOrdIdToInternalId_.find(request.origClOrdId);
            if (it != clOrdIdToInternalId_.end()) {
                auto stateIt = ordersByInternalId_.find(it->second);
                if (stateIt != ordersByInternalId_.end() && !stateIt->second.terminal &&
                    stateIt->second.side == request.side) {
                    internalId = it->second;
                    seenClOrdIds_.insert(request.clOrdId);
                }
            }
        }
    }

    if (internalId == 0) {
        reject(request.clOrdId, request.origClOrdId, request.symbol, request.side,
               "Unknown OrigClOrdID, duplicate ClOrdID, terminal order, or side mismatch");
        return 0;
    }

    uint64_t seq = engine_.submitCancelOrder(internalId);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingClientIdsBySequence_[seq] = {request.clOrdId, request.origClOrdId};
    }
    return seq;
}

uint64_t FixApp::submitCancelReplace(const FixCancelReplaceRequest& request) {
    if (request.clOrdId.empty() || request.origClOrdId.empty()) {
        reject(request.clOrdId, request.origClOrdId, request.symbol, request.side, "ClOrdID and OrigClOrdID are required");
        return 0;
    }
    if (request.orderQty <= 0 || request.price <= 0) {
        reject(request.clOrdId, request.origClOrdId, request.symbol, request.side, "Replacement quantity and price must be positive");
        return 0;
    }

    int internalId = 0;
    FixExecutionReport replaceAck;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (seenClOrdIds_.find(request.clOrdId) != seenClOrdIds_.end()) {
            internalId = 0;
        } else {
            auto mapIt = clOrdIdToInternalId_.find(request.origClOrdId);
            if (mapIt != clOrdIdToInternalId_.end()) {
                internalId = mapIt->second;
                auto stateIt = ordersByInternalId_.find(internalId);
                if (stateIt != ordersByInternalId_.end() && !stateIt->second.terminal && stateIt->second.side == request.side) {
                    OrderState& state = stateIt->second;
                    seenClOrdIds_.insert(request.clOrdId);
                    clOrdIdToInternalId_[request.clOrdId] = internalId;
                    state.activeClOrdId = request.clOrdId;
                    state.symbol = request.symbol;
                    state.leavesQty = request.orderQty;
                    state.orderQty = state.cumQty + request.orderQty;
                    replaceAck = makeBaseReport(state, FixExecType::Replace, FixOrdStatus::Replaced);
                    replaceAck.origClOrdId = request.origClOrdId;
                } else {
                    internalId = 0;
                }
            }
        }
    }

    if (internalId == 0) {
        reject(request.clOrdId, request.origClOrdId, request.symbol, request.side,
               "Unknown OrigClOrdID, duplicate ClOrdID, terminal order, or side mismatch");
        return 0;
    }

    replaceAck.execId = nextExecId();
    emitReport(replaceAck);

    uint64_t seq = engine_.submitModifyOrder(OrderModify(internalId, request.side, request.price, request.orderQty));
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingClientIdsBySequence_[seq] = {request.clOrdId, request.origClOrdId};
    }
    return seq;
}

void FixApp::removeActiveMappingsFor(int internalId) {
    for (auto it = clOrdIdToInternalId_.begin(); it != clOrdIdToInternalId_.end();) {
        if (it->second == internalId) {
            it = clOrdIdToInternalId_.erase(it);
        } else {
            ++it;
        }
    }
}

void FixApp::onTrade(const Trade& trade) {
    const struct FillSide { int internalId; int qty; int px; } fills[] = {
        {trade.getBuyOrderId(), trade.getQuantity(), trade.getPrice()},
        {trade.getSellOrderId(), trade.getQuantity(), trade.getPrice()}
    };

    for (const auto& fill : fills) {
        FixExecutionReport report;
        bool shouldEmit = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = ordersByInternalId_.find(fill.internalId);
            if (it != ordersByInternalId_.end() && !it->second.terminal) {
                OrderState& state = it->second;
                state.cumQty += fill.qty;
                state.leavesQty = std::max(0, state.leavesQty - fill.qty);
                state.notional += static_cast<long long>(fill.qty) * fill.px;
                FixExecType execType = state.leavesQty == 0 ? FixExecType::Fill : FixExecType::PartialFill;
                FixOrdStatus ordStatus = state.leavesQty == 0 ? FixOrdStatus::Filled : FixOrdStatus::PartiallyFilled;
                report = makeBaseReport(state, execType, ordStatus);
                report.lastQty = fill.qty;
                report.lastPx = fill.px;
                if (state.leavesQty == 0) {
                    state.terminal = true;
                    removeActiveMappingsFor(state.internalOrderId);
                }
                shouldEmit = true;
            }
        }
        if (shouldEmit) {
            report.execId = nextExecId();
            emitReport(report);
        }
    }
}

void FixApp::onOrderEvent(const OrderEventResult& result) {
    std::pair<std::string, std::string> clientIds;
    bool hasPending = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto pendingIt = pendingClientIdsBySequence_.find(result.sequenceNumber);
        if (pendingIt != pendingClientIdsBySequence_.end()) {
            clientIds = pendingIt->second;
            pendingClientIdsBySequence_.erase(pendingIt);
            hasPending = true;
        }
    }

    if (!result.success) {
        reject(hasPending ? clientIds.first : std::to_string(result.orderId),
               hasPending ? clientIds.second : std::string{}, "STOCK", Side::Buy, result.errorMessage);
        if (result.eventType == EventType::NewOrder) {
            std::lock_guard<std::mutex> lock(mutex_);
            ordersByInternalId_.erase(result.orderId);
            removeActiveMappingsFor(result.orderId);
        }
        return;
    }

    if (result.eventType == EventType::CancelOrder) {
        FixExecutionReport report;
        bool shouldEmit = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = ordersByInternalId_.find(result.orderId);
            if (it != ordersByInternalId_.end() && !it->second.terminal) {
                OrderState& state = it->second;
                state.activeClOrdId = hasPending ? clientIds.first : state.activeClOrdId;
                report = makeBaseReport(state, FixExecType::Canceled, FixOrdStatus::Canceled);
                report.origClOrdId = hasPending ? clientIds.second : std::string{};
                report.leavesQty = 0;
                state.leavesQty = 0;
                state.terminal = true;
                removeActiveMappingsFor(state.internalOrderId);
                shouldEmit = true;
            }
        }
        if (shouldEmit) {
            report.execId = nextExecId();
            emitReport(report);
        }
        return;
    }

    if (result.eventType == EventType::NewOrder) {
        FixExecutionReport cancelReport;
        bool shouldEmitCancel = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = ordersByInternalId_.find(result.orderId);
            if (it != ordersByInternalId_.end() && !it->second.terminal &&
                (it->second.orderType == OrderType::Market || it->second.orderType == OrderType::FillAndKill)) {
                OrderState& state = it->second;
                if (state.leavesQty > 0) {
                    cancelReport = makeBaseReport(state, FixExecType::Canceled, FixOrdStatus::Canceled);
                    cancelReport.text = state.orderType == OrderType::Market
                        ? "Unfilled market quantity canceled"
                        : "Unfilled FAK quantity canceled";
                    state.leavesQty = 0;
                    state.terminal = true;
                    removeActiveMappingsFor(state.internalOrderId);
                    shouldEmitCancel = true;
                }
            }
        }
        if (shouldEmitCancel) {
            cancelReport.leavesQty = 0;
            cancelReport.execId = nextExecId();
            emitReport(cancelReport);
        }
    }
}

void FixApp::runDemo() {
    std::cout << "[FixApp] FIX 4.4 adapter ready; QuickFIX network mode is optional in this build.\n";
}

#if FIXAPP_HAS_QUICKFIX

void FixApp::onCreate(const FIX::SessionID& sessionID) {
    std::cout << "[QuickFIX44] Session created: " << sessionID << std::endl;
}

void FixApp::onLogon(const FIX::SessionID& sessionID) {
    std::cout << "[QuickFIX44] Logon: " << sessionID << std::endl;
    sessionID_ = sessionID;
}

void FixApp::onLogout(const FIX::SessionID& sessionID) {
    std::cout << "[QuickFIX44] Logout: " << sessionID << std::endl;
}

void FixApp::fromAdmin(const FIX::Message&, const FIX::SessionID&) {}
void FixApp::toAdmin(FIX::Message&, const FIX::SessionID&) {}
void FixApp::toApp(FIX::Message&, const FIX::SessionID&) {}

void FixApp::fromApp(const FIX::Message& message, const FIX::SessionID& sessionID) {
    crack(message, sessionID);
}

void FixApp::onMessage(const FIX44::NewOrderSingle& orderMsg, const FIX::SessionID& sessionID) {
    sessionID_ = sessionID;
    FIX::ClOrdID clOrdID;
    FIX::Symbol symbol;
    FIX::Side side;
    FIX::OrderQty orderQty;
    FIX::OrdType ordType;
    orderMsg.get(clOrdID);
    orderMsg.get(symbol);
    orderMsg.get(side);
    orderMsg.get(orderQty);
    orderMsg.get(ordType);

    FixNewOrderRequest request;
    request.clOrdId = clOrdID.getString();
    request.symbol = symbol.getString();
    request.side = side.getValue() == FIX::Side_BUY ? Side::Buy : Side::Sell;
    request.orderQty = static_cast<int>(orderQty.getValue());

    if (ordType.getValue() == FIX::OrdType_MARKET) {
        request.orderType = OrderType::Market;
    } else if (ordType.getValue() == FIX::OrdType_LIMIT) {
        FIX::TimeInForce tif;
        if (orderMsg.isSetField(tif)) {
            orderMsg.get(tif);
            request.orderType = tif.getValue() == FIX::TimeInForce_IMMEDIATE_OR_CANCEL
                ? OrderType::FillAndKill
                : OrderType::Limit;
        } else {
            request.orderType = OrderType::Limit;
        }
        FIX::Price price;
        orderMsg.get(price);
        request.price = static_cast<int>(price.getValue());
    } else {
        reject(request.clOrdId, {}, request.symbol, request.side, "Unsupported OrdType");
        return;
    }

    submitNewOrder(request);
}

void FixApp::onMessage(const FIX44::OrderCancelRequest& cancelMsg, const FIX::SessionID& sessionID) {
    sessionID_ = sessionID;
    FIX::ClOrdID clOrdID;
    FIX::OrigClOrdID origClOrdID;
    FIX::Symbol symbol;
    FIX::Side side;
    cancelMsg.get(clOrdID);
    cancelMsg.get(origClOrdID);
    cancelMsg.get(symbol);
    cancelMsg.get(side);

    submitCancel(FixCancelRequest{clOrdID.getString(), origClOrdID.getString(), symbol.getString(),
                                  side.getValue() == FIX::Side_BUY ? Side::Buy : Side::Sell});
}

void FixApp::onMessage(const FIX44::OrderCancelReplaceRequest& replaceMsg, const FIX::SessionID& sessionID) {
    sessionID_ = sessionID;
    FIX::ClOrdID clOrdID;
    FIX::OrigClOrdID origClOrdID;
    FIX::Symbol symbol;
    FIX::Side side;
    FIX::OrderQty orderQty;
    FIX::Price price;
    replaceMsg.get(clOrdID);
    replaceMsg.get(origClOrdID);
    replaceMsg.get(symbol);
    replaceMsg.get(side);
    replaceMsg.get(orderQty);
    replaceMsg.get(price);

    submitCancelReplace(FixCancelReplaceRequest{clOrdID.getString(), origClOrdID.getString(), symbol.getString(),
                                                side.getValue() == FIX::Side_BUY ? Side::Buy : Side::Sell,
                                                static_cast<int>(orderQty.getValue()),
                                                static_cast<int>(price.getValue())});
}

void FixApp::sendQuickFixReport(const FixExecutionReport& report) {
    FIX44::ExecutionReport execReport;
    execReport.set(FIX::OrderID(report.orderId));
    execReport.set(FIX::ExecID(report.execId));
    execReport.set(FIX::ExecType(toFixExecType(report.execType)[0]));
    execReport.set(FIX::OrdStatus(toFixOrdStatus(report.ordStatus)[0]));
    execReport.set(FIX::Symbol(report.symbol));
    execReport.set(FIX::Side(report.side == Side::Buy ? FIX::Side_BUY : FIX::Side_SELL));
    execReport.set(FIX::LeavesQty(report.leavesQty));
    execReport.set(FIX::CumQty(report.cumQty));
    execReport.set(FIX::AvgPx(report.avgPx));
    execReport.set(FIX::ClOrdID(report.clOrdId));
    if (!report.origClOrdId.empty()) {
        execReport.set(FIX::OrigClOrdID(report.origClOrdId));
    }
    if (report.lastQty > 0) {
        execReport.set(FIX::LastQty(report.lastQty));
        execReport.set(FIX::LastPx(report.lastPx));
    }
    if (!report.text.empty()) {
        execReport.set(FIX::Text(report.text));
    }
    FIX::Session::sendToTarget(execReport, sessionID_);
}

#endif // FIXAPP_HAS_QUICKFIX