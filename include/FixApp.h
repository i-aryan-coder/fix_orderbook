#ifndef FIXAPP_H
#define FIXAPP_H

#include "MatchingEngine.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <unordered_set>
#include <vector>

#if __has_include("quickfix/Application.h")
#define FIXAPP_HAS_QUICKFIX 1
#include "quickfix/Application.h"
#include "quickfix/MessageCracker.h"
#include "quickfix/Session.h"
#include "quickfix/fix44/ExecutionReport.h"
#include "quickfix/fix44/NewOrderSingle.h"
#include "quickfix/fix44/OrderCancelReplaceRequest.h"
#include "quickfix/fix44/OrderCancelRequest.h"
#else
#define FIXAPP_HAS_QUICKFIX 0
#endif

struct FixNewOrderRequest {
    std::string clOrdId;
    std::string symbol{"STOCK"};
    Side side{Side::Buy};
    OrderType orderType{OrderType::Limit};
    int orderQty{0};
    int price{0};
};

struct FixCancelRequest {
    std::string clOrdId;
    std::string origClOrdId;
    std::string symbol{"STOCK"};
    Side side{Side::Buy};
};

struct FixCancelReplaceRequest {
    std::string clOrdId;
    std::string origClOrdId;
    std::string symbol{"STOCK"};
    Side side{Side::Buy};
    int orderQty{0};
    int price{0};
};

enum class FixExecType {
    New,
    PartialFill,
    Fill,
    Canceled,
    Replace,
    Rejected
};

enum class FixOrdStatus {
    New,
    PartiallyFilled,
    Filled,
    Canceled,
    Replaced,
    Rejected
};

struct FixExecutionReport {
    std::string clOrdId;
    std::string origClOrdId;
    std::string orderId;
    std::string execId;
    FixExecType execType{FixExecType::New};
    FixOrdStatus ordStatus{FixOrdStatus::New};
    std::string symbol{"STOCK"};
    Side side{Side::Buy};
    int orderQty{0};
    int lastQty{0};
    int lastPx{0};
    int cumQty{0};
    int leavesQty{0};
    double avgPx{0.0};
    std::string text;
};

/**
 * @brief FIX 4.4 protocol adapter for the MatchingEngine.
 *
 * FixApp owns client-order-ID mapping and FIX lifecycle reporting, but submits all
 * mutations through MatchingEngine so the core engine remains FIX-agnostic.
 */
class FixApp
#if FIXAPP_HAS_QUICKFIX
    : public FIX::Application, public FIX::MessageCracker
#endif
{
public:
    using ReportListener = std::function<void(const FixExecutionReport&)>;

    explicit FixApp(MatchingEngine& engine);
    ~FixApp() = default;

    FixApp(const FixApp&) = delete;
    FixApp& operator=(const FixApp&) = delete;

#if FIXAPP_HAS_QUICKFIX
    void onCreate(const FIX::SessionID& sessionID) override;
    void onLogon(const FIX::SessionID& sessionID) override;
    void onLogout(const FIX::SessionID& sessionID) override;

    void fromAdmin(const FIX::Message& message, const FIX::SessionID& sessionID) override;
    void toAdmin(FIX::Message& message, const FIX::SessionID& sessionID) override;
    void toApp(FIX::Message& message, const FIX::SessionID& sessionID) override;
    void fromApp(const FIX::Message& message, const FIX::SessionID& sessionID) override;

    void onMessage(const FIX44::NewOrderSingle& orderMsg, const FIX::SessionID& sessionID) override;
    void onMessage(const FIX44::OrderCancelRequest& cancelMsg, const FIX::SessionID& sessionID) override;
    void onMessage(const FIX44::OrderCancelReplaceRequest& replaceMsg, const FIX::SessionID& sessionID) override;
#endif

    uint64_t submitNewOrder(const FixNewOrderRequest& request);
    uint64_t submitCancel(const FixCancelRequest& request);
    uint64_t submitCancelReplace(const FixCancelReplaceRequest& request);

    void addReportListener(ReportListener listener);
    std::vector<FixExecutionReport> getReports() const;
    std::optional<int> getInternalOrderId(const std::string& clOrdId) const;
    void runDemo();

private:
    struct OrderState {
        int internalOrderId{0};
        std::string activeClOrdId;
        std::string symbol;
        Side side{Side::Buy};
        OrderType orderType{OrderType::Limit};
        int orderQty{0};
        int cumQty{0};
        int leavesQty{0};
        long long notional{0};
        bool terminal{false};
    };

    uint64_t nextInternalOrderId();
    std::string nextExecId();
    void onOrderEvent(const OrderEventResult& result);
    void onTrade(const Trade& trade);
    void emitReport(const FixExecutionReport& report);
    FixExecutionReport makeBaseReport(const OrderState& state, FixExecType execType, FixOrdStatus ordStatus) const;
    void reject(const std::string& clOrdId, const std::string& origClOrdId, const std::string& symbol,
                Side side, const std::string& reason);
    std::shared_ptr<Order> makeOrder(int internalId, const FixNewOrderRequest& request) const;
    void removeActiveMappingsFor(int internalId);

#if FIXAPP_HAS_QUICKFIX
    void sendQuickFixReport(const FixExecutionReport& report);
#endif

    MatchingEngine& engine_;
    std::atomic<int> nextOrderId_{1};
    std::atomic<uint64_t> nextExecId_{1};

    mutable std::mutex mutex_;
    std::unordered_map<std::string, int> clOrdIdToInternalId_;
    std::unordered_map<int, OrderState> ordersByInternalId_;
    std::unordered_map<uint64_t, std::pair<std::string, std::string>> pendingClientIdsBySequence_;
    std::unordered_set<std::string> seenClOrdIds_;
    std::vector<FixExecutionReport> reports_;
    std::vector<ReportListener> reportListeners_;

#if FIXAPP_HAS_QUICKFIX
    FIX::SessionID sessionID_;
#endif
};

const char* toFixExecType(FixExecType type);
const char* toFixOrdStatus(FixOrdStatus status);

#endif // FIXAPP_H