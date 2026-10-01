#include "FixApp.h"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

static int g_fixTotalTests = 0;
static int g_fixPassedTests = 0;
static int g_fixFailedTests = 0;

#define TEST_CASE(name) \
    void name(); \
    struct Register_##name { \
        Register_##name() { \
            g_fixTotalTests++; \
            try { \
                std::cout << "[RUN ] " << #name << std::endl; \
                name(); \
                g_fixPassedTests++; \
                std::cout << "[PASS] " << #name << "\n" << std::endl; \
            } catch (const std::exception& e) { \
                g_fixFailedTests++; \
                std::cerr << "[FAIL] " << #name << " - Exception: " << e.what() << "\n" << std::endl; \
            } catch (...) { \
                g_fixFailedTests++; \
                std::cerr << "[FAIL] " << #name << " - Unknown exception\n" << std::endl; \
            } \
        } \
    } instance_##name; \
    void name()

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            throw std::runtime_error(std::string("Assertion failed: ") + #cond + " at line " + std::to_string(__LINE__)); \
        } \
    } while(0)

#define ASSERT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            throw std::runtime_error(std::string("Assertion failed: ") + #a + " == " + #b + \
                " at line " + std::to_string(__LINE__)); \
        } \
    } while(0)

TEST_CASE(TestF01_Fix44EnumMappings) {
    ASSERT_EQ(std::string(toFixExecType(FixExecType::New)), "0");
    ASSERT_EQ(std::string(toFixExecType(FixExecType::PartialFill)), "1");
    ASSERT_EQ(std::string(toFixExecType(FixExecType::Fill)), "2");
    ASSERT_EQ(std::string(toFixExecType(FixExecType::Canceled)), "4");
    ASSERT_EQ(std::string(toFixExecType(FixExecType::Replace)), "5");
    ASSERT_EQ(std::string(toFixExecType(FixExecType::Rejected)), "8");
}

TEST_CASE(TestF02_NewOrderCreatesInternalMapping) {
    MatchingEngine engine;
    FixApp app(engine);
    engine.start();

    uint64_t seq = app.submitNewOrder(FixNewOrderRequest{"ABC-1", "AAPL", Side::Buy, OrderType::Limit, 10, 100});
    engine.waitForSequence(seq);

    ASSERT_TRUE(app.getInternalOrderId("ABC-1").has_value());
    auto reports = app.getReports();
    ASSERT_EQ(reports.size(), 1u);
    ASSERT_EQ(reports[0].clOrdId, "ABC-1");
    ASSERT_EQ(reports[0].execType, FixExecType::New);
    ASSERT_EQ(reports[0].leavesQty, 10);

    engine.stop(true);
}

TEST_CASE(TestF03_LimitOrderNewExecutionReport) {
    MatchingEngine engine;
    FixApp app(engine);
    engine.start();

    auto seq = app.submitNewOrder(FixNewOrderRequest{"LMT-1", "MSFT", Side::Sell, OrderType::Limit, 7, 105});
    engine.waitForSequence(seq);

    auto reports = app.getReports();
    ASSERT_EQ(reports[0].execType, FixExecType::New);
    ASSERT_EQ(reports[0].ordStatus, FixOrdStatus::New);
    ASSERT_EQ(reports[0].orderQty, 7);
    ASSERT_EQ(reports[0].cumQty, 0);
    ASSERT_EQ(reports[0].leavesQty, 7);

    engine.stop(true);
}

TEST_CASE(TestF04_MarketOrderExecutionReports) {
    MatchingEngine engine;
    FixApp app(engine);
    engine.start();

    auto askSeq = app.submitNewOrder(FixNewOrderRequest{"ASK-1", "MSFT", Side::Sell, OrderType::Limit, 10, 100});
    engine.waitForSequence(askSeq);
    auto mktSeq = app.submitNewOrder(FixNewOrderRequest{"MKT-1", "MSFT", Side::Buy, OrderType::Market, 6, 0});
    engine.waitForSequence(mktSeq);

    auto reports = app.getReports();
    ASSERT_EQ(reports.size(), 4u);
    ASSERT_EQ(reports[2].clOrdId, "MKT-1");
    ASSERT_EQ(reports[2].execType, FixExecType::Fill);
    ASSERT_EQ(reports[2].lastQty, 6);
    ASSERT_EQ(reports[2].lastPx, 100);
    ASSERT_EQ(reports[2].cumQty, 6);
    ASSERT_EQ(reports[2].leavesQty, 0);
    ASSERT_EQ(reports[3].clOrdId, "ASK-1");
    ASSERT_EQ(reports[3].execType, FixExecType::PartialFill);

    engine.stop(true);
}

TEST_CASE(TestF05_FakOrderPartialThenCancel) {
    MatchingEngine engine;
    FixApp app(engine);
    engine.start();

    engine.waitForSequence(app.submitNewOrder(FixNewOrderRequest{"ASK-FAK", "MSFT", Side::Sell, OrderType::Limit, 5, 100}));
    auto seq = app.submitNewOrder(FixNewOrderRequest{"FAK-1", "MSFT", Side::Buy, OrderType::FillAndKill, 10, 100});
    engine.waitForSequence(seq);

    auto reports = app.getReports();
    ASSERT_EQ(reports[2].clOrdId, "FAK-1");
    ASSERT_EQ(reports[2].execType, FixExecType::PartialFill);
    ASSERT_EQ(reports[2].cumQty, 5);
    ASSERT_EQ(reports[2].leavesQty, 5);
    ASSERT_EQ(reports[4].clOrdId, "FAK-1");
    ASSERT_EQ(reports[4].execType, FixExecType::Canceled);
    ASSERT_EQ(reports[4].cumQty, 5);
    ASSERT_EQ(reports[4].leavesQty, 0);

    engine.stop(true);
}

TEST_CASE(TestF06_PartialFillReport) {
    MatchingEngine engine;
    FixApp app(engine);
    engine.start();

    engine.waitForSequence(app.submitNewOrder(FixNewOrderRequest{"S1", "IBM", Side::Sell, OrderType::Limit, 10, 50}));
    engine.waitForSequence(app.submitNewOrder(FixNewOrderRequest{"B1", "IBM", Side::Buy, OrderType::Limit, 4, 50}));

    auto reports = app.getReports();
    ASSERT_EQ(reports[2].execType, FixExecType::Fill);
    ASSERT_EQ(reports[2].lastQty, 4);
    ASSERT_EQ(reports[3].execType, FixExecType::PartialFill);
    ASSERT_EQ(reports[3].cumQty, 4);
    ASSERT_EQ(reports[3].leavesQty, 6);

    engine.stop(true);
}

TEST_CASE(TestF07_FullFillReport) {
    MatchingEngine engine;
    FixApp app(engine);
    engine.start();

    engine.waitForSequence(app.submitNewOrder(FixNewOrderRequest{"S2", "IBM", Side::Sell, OrderType::Limit, 5, 50}));
    engine.waitForSequence(app.submitNewOrder(FixNewOrderRequest{"B2", "IBM", Side::Buy, OrderType::Limit, 5, 50}));

    auto reports = app.getReports();
    ASSERT_EQ(reports[2].clOrdId, "B2");
    ASSERT_EQ(reports[2].execType, FixExecType::Fill);
    ASSERT_EQ(reports[2].leavesQty, 0);
    ASSERT_EQ(reports[3].clOrdId, "S2");
    ASSERT_EQ(reports[3].execType, FixExecType::Fill);

    engine.stop(true);
}

TEST_CASE(TestF08_MultipleFillsCumulativeState) {
    MatchingEngine engine;
    FixApp app(engine);
    engine.start();

    engine.waitForSequence(app.submitNewOrder(FixNewOrderRequest{"S3", "IBM", Side::Sell, OrderType::Limit, 30, 50}));
    engine.waitForSequence(app.submitNewOrder(FixNewOrderRequest{"S4", "IBM", Side::Sell, OrderType::Limit, 40, 51}));
    engine.waitForSequence(app.submitNewOrder(FixNewOrderRequest{"S5", "IBM", Side::Sell, OrderType::Limit, 30, 52}));
    engine.waitForSequence(app.submitNewOrder(FixNewOrderRequest{"B3", "IBM", Side::Buy, OrderType::Market, 100, 0}));

    auto reports = app.getReports();
    std::vector<FixExecutionReport> buyFills;
    for (const auto& report : reports) {
        if (report.clOrdId == "B3" && report.lastQty > 0) {
            buyFills.push_back(report);
        }
    }
    ASSERT_EQ(buyFills.size(), 3u);
    ASSERT_EQ(buyFills[0].lastQty, 30);
    ASSERT_EQ(buyFills[0].cumQty, 30);
    ASSERT_EQ(buyFills[0].leavesQty, 70);
    ASSERT_EQ(buyFills[1].lastQty, 40);
    ASSERT_EQ(buyFills[1].cumQty, 70);
    ASSERT_EQ(buyFills[1].leavesQty, 30);
    ASSERT_EQ(buyFills[2].lastQty, 30);
    ASSERT_EQ(buyFills[2].cumQty, 100);
    ASSERT_EQ(buyFills[2].leavesQty, 0);

    engine.stop(true);
}

TEST_CASE(TestF09_LastQtyLastPxFromTrade) {
    MatchingEngine engine;
    FixApp app(engine);
    engine.start();

    engine.waitForSequence(app.submitNewOrder(FixNewOrderRequest{"REST", "TSLA", Side::Sell, OrderType::Limit, 10, 123}));
    engine.waitForSequence(app.submitNewOrder(FixNewOrderRequest{"AGGR", "TSLA", Side::Buy, OrderType::Limit, 2, 130}));

    auto reports = app.getReports();
    ASSERT_EQ(reports[2].lastQty, 2);
    ASSERT_EQ(reports[2].lastPx, 123);

    engine.stop(true);
}

TEST_CASE(TestF10_ExecIdUniqueness) {
    MatchingEngine engine;
    FixApp app(engine);
    engine.start();

    engine.waitForSequence(app.submitNewOrder(FixNewOrderRequest{"E1", "X", Side::Sell, OrderType::Limit, 1, 10}));
    engine.waitForSequence(app.submitNewOrder(FixNewOrderRequest{"E2", "X", Side::Buy, OrderType::Limit, 1, 10}));

    auto reports = app.getReports();
    ASSERT_TRUE(reports[0].execId != reports[1].execId);
    ASSERT_TRUE(reports[1].execId != reports[2].execId);
    ASSERT_TRUE(reports[2].execId != reports[3].execId);

    engine.stop(true);
}

TEST_CASE(TestF11_ClOrdIdPreserved) {
    MatchingEngine engine;
    FixApp app(engine);
    engine.start();

    engine.waitForSequence(app.submitNewOrder(FixNewOrderRequest{"CLIENT-STRING-ID", "X", Side::Buy, OrderType::Limit, 1, 10}));
    ASSERT_EQ(app.getReports()[0].clOrdId, "CLIENT-STRING-ID");

    engine.stop(true);
}

TEST_CASE(TestF12_CancelRequestAccepted) {
    MatchingEngine engine;
    FixApp app(engine);
    engine.start();

    engine.waitForSequence(app.submitNewOrder(FixNewOrderRequest{"C1", "X", Side::Buy, OrderType::Limit, 10, 10}));
    engine.waitForSequence(app.submitCancel(FixCancelRequest{"CXL-1", "C1", "X", Side::Buy}));

    auto reports = app.getReports();
    ASSERT_EQ(reports.back().clOrdId, "CXL-1");
    ASSERT_EQ(reports.back().origClOrdId, "C1");
    ASSERT_EQ(reports.back().execType, FixExecType::Canceled);
    ASSERT_EQ(reports.back().leavesQty, 0);

    engine.stop(true);
}

TEST_CASE(TestF13_CancelRejectUnknownClOrdId) {
    MatchingEngine engine;
    FixApp app(engine);
    engine.start();

    app.submitCancel(FixCancelRequest{"CXL-UNKNOWN", "NO-SUCH", "X", Side::Buy});
    auto reports = app.getReports();
    ASSERT_EQ(reports.size(), 1u);
    ASSERT_EQ(reports[0].execType, FixExecType::Rejected);

    engine.stop(true);
}

TEST_CASE(TestF14_CancelReplaceAccepted) {
    MatchingEngine engine;
    FixApp app(engine);
    engine.start();

    engine.waitForSequence(app.submitNewOrder(FixNewOrderRequest{"R1", "X", Side::Buy, OrderType::Limit, 10, 10}));
    engine.waitForSequence(app.submitCancelReplace(FixCancelReplaceRequest{"R2", "R1", "X", Side::Buy, 4, 10}));

    auto reports = app.getReports();
    ASSERT_EQ(reports[1].clOrdId, "R2");
    ASSERT_EQ(reports[1].origClOrdId, "R1");
    ASSERT_EQ(reports[1].execType, FixExecType::Replace);
    ASSERT_EQ(reports[1].leavesQty, 4);

    engine.stop(true);
}

TEST_CASE(TestF15_ReplacePrioritySemanticsPreserved) {
    MatchingEngine engine;
    FixApp app(engine);
    engine.start();

    engine.waitForSequence(app.submitNewOrder(FixNewOrderRequest{"B-FIRST", "X", Side::Buy, OrderType::Limit, 5, 100}));
    engine.waitForSequence(app.submitNewOrder(FixNewOrderRequest{"B-SECOND", "X", Side::Buy, OrderType::Limit, 5, 100}));
    engine.waitForSequence(app.submitCancelReplace(FixCancelReplaceRequest{"B-FIRST-R", "B-FIRST", "X", Side::Buy, 10, 100}));
    engine.waitForSequence(app.submitNewOrder(FixNewOrderRequest{"S-AGGR", "X", Side::Sell, OrderType::Limit, 6, 100}));

    auto reports = app.getReports();
    std::vector<std::string> filledBuyIds;
    for (const auto& report : reports) {
        if (report.side == Side::Buy && report.lastQty > 0) {
            filledBuyIds.push_back(report.clOrdId);
        }
    }
    ASSERT_EQ(filledBuyIds.size(), 2u);
    ASSERT_EQ(filledBuyIds[0], "B-SECOND");
    ASSERT_EQ(filledBuyIds[1], "B-FIRST-R");

    engine.stop(true);
}

TEST_CASE(TestF16_InvalidOrderRejected) {
    MatchingEngine engine;
    FixApp app(engine);
    engine.start();

    app.submitNewOrder(FixNewOrderRequest{"BAD", "X", Side::Buy, OrderType::Limit, 0, 10});
    auto reports = app.getReports();
    ASSERT_EQ(reports.size(), 1u);
    ASSERT_EQ(reports[0].execType, FixExecType::Rejected);

    engine.stop(true);
}

TEST_CASE(TestF17_DuplicateClOrdIdRejected) {
    MatchingEngine engine;
    FixApp app(engine);
    engine.start();

    engine.waitForSequence(app.submitNewOrder(FixNewOrderRequest{"DUP", "X", Side::Buy, OrderType::Limit, 1, 10}));
    app.submitNewOrder(FixNewOrderRequest{"DUP", "X", Side::Buy, OrderType::Limit, 1, 11});

    auto reports = app.getReports();
    ASSERT_EQ(reports.back().execType, FixExecType::Rejected);

    engine.stop(true);
}

TEST_CASE(TestF18_BothSidesReceiveExecutionReports) {
    MatchingEngine engine;
    FixApp app(engine);
    engine.start();

    engine.waitForSequence(app.submitNewOrder(FixNewOrderRequest{"SELLER", "X", Side::Sell, OrderType::Limit, 3, 10}));
    engine.waitForSequence(app.submitNewOrder(FixNewOrderRequest{"BUYER", "X", Side::Buy, OrderType::Limit, 3, 10}));

    bool sellerFill = false;
    bool buyerFill = false;
    for (const auto& report : app.getReports()) {
        sellerFill = sellerFill || (report.clOrdId == "SELLER" && report.execType == FixExecType::Fill);
        buyerFill = buyerFill || (report.clOrdId == "BUYER" && report.execType == FixExecType::Fill);
    }
    ASSERT_TRUE(sellerFill);
    ASSERT_TRUE(buyerFill);

    engine.stop(true);
}

TEST_CASE(TestF19_FixLayerDoesNotMutateBeforeQueueProcessing) {
    MatchingEngine engine;
    FixApp app(engine);
    engine.start();

    uint64_t seq = app.submitNewOrder(FixNewOrderRequest{"QUEUE-ONLY", "X", Side::Buy, OrderType::Limit, 5, 10});
    ASSERT_TRUE(seq > 0);
    engine.waitForSequence(seq);
    auto snap = engine.getSnapshot();
    ASSERT_EQ(snap->getbids().size(), 1u);
    ASSERT_EQ(snap->getbids()[0].quantity, 5);

    engine.stop(true);
}

TEST_CASE(TestF20_ReplaceRejectSideMismatch) {
    MatchingEngine engine;
    FixApp app(engine);
    engine.start();

    engine.waitForSequence(app.submitNewOrder(FixNewOrderRequest{"SIDE-1", "X", Side::Buy, OrderType::Limit, 5, 10}));
    app.submitCancelReplace(FixCancelReplaceRequest{"SIDE-2", "SIDE-1", "X", Side::Sell, 5, 10});

    auto reports = app.getReports();
    ASSERT_EQ(reports.back().execType, FixExecType::Rejected);

    engine.stop(true);
}

TEST_CASE(TestF21_CancelRejectSideMismatch) {
    MatchingEngine engine;
    FixApp app(engine);
    engine.start();

    engine.waitForSequence(app.submitNewOrder(FixNewOrderRequest{"CXL-SIDE-1", "X", Side::Buy, OrderType::Limit, 5, 10}));
    app.submitCancel(FixCancelRequest{"CXL-SIDE-2", "CXL-SIDE-1", "X", Side::Sell});

    auto reports = app.getReports();
    ASSERT_EQ(reports.back().execType, FixExecType::Rejected);

    engine.stop(true);
}

int main() {
    std::cout << "=========================================================\n";
    std::cout << "  FIX 4.4 Adapter Test Suite (Phase 3)\n";
    std::cout << "=========================================================\n";
    std::cout << "Total FIX Tests Executed: " << g_fixTotalTests << "\n";
    std::cout << "Passed: " << g_fixPassedTests << "\n";
    std::cout << "Failed: " << g_fixFailedTests << "\n";
    std::cout << "=========================================================\n";

    if (g_fixFailedTests == 0) {
        std::cout << "ALL " << g_fixPassedTests << " FIX ADAPTER TESTS PASSED SUCCESSFULLY!\n";
        return 0;
    }
    std::cerr << "FAILURES DETECTED: " << g_fixFailedTests << " FIX adapter tests failed!\n";
    return 1;
}
