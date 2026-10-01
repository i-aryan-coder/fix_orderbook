#include "Orderbook.h"
#include <iostream>
#include <cassert>
#include <string>
#include <vector>

/* -----------------------------------------------------
   Lightweight Test Harness
   ----------------------------------------------------- */

static int g_totalTests = 0;
static int g_passedTests = 0;
static int g_failedTests = 0;

#define TEST_CASE(name) \
    void name(); \
    struct Register_##name { \
        Register_##name() { \
            g_totalTests++; \
            try { \
                std::cout << "[RUN ] " << #name << std::endl; \
                name(); \
                g_passedTests++; \
                std::cout << "[PASS] " << #name << "\n" << std::endl; \
            } catch (const std::exception& e) { \
                g_failedTests++; \
                std::cerr << "[FAIL] " << #name << " - Exception: " << e.what() << "\n" << std::endl; \
            } catch (...) { \
                g_failedTests++; \
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
                " (" + std::to_string(a) + " != " + std::to_string(b) + ") at line " + std::to_string(__LINE__)); \
        } \
    } while(0)

#define ASSERT_THROWS(expr, ExceptionType) \
    do { \
        bool caught = false; \
        try { \
            expr; \
        } catch (const ExceptionType&) { \
            caught = true; \
        } catch (...) { \
            throw std::runtime_error(std::string("Wrong exception thrown for ") + #expr + " at line " + std::to_string(__LINE__)); \
        } \
        if (!caught) { \
            throw std::runtime_error(std::string("Expected exception not thrown for ") + #expr + " at line " + std::to_string(__LINE__)); \
        } \
    } while(0)

/* -----------------------------------------------------
   LIMIT ORDER TESTS (1 - 5)
   ----------------------------------------------------- */

// 1. Buy/Sell crossing
TEST_CASE(Test01_LimitBuySellCrossing) {
    Orderbook ob;
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 1, Side::Sell, 100, 10));
    auto trades = ob.addorder(std::make_shared<Order>(OrderType::Limit, 2, Side::Buy, 100, 10));

    ASSERT_EQ(trades.size(), 1u);
    ASSERT_EQ(trades[0].getBuyOrderId(), 2);
    ASSERT_EQ(trades[0].getSellOrderId(), 1);
    ASSERT_EQ(trades[0].getPrice(), 100);
    ASSERT_EQ(trades[0].getQuantity(), 10);
    ASSERT_EQ(ob.ordercount(), 0u);
}

// 2. Non-crossing orders resting in book
TEST_CASE(Test02_NonCrossingOrdersResting) {
    Orderbook ob;
    auto trades1 = ob.addorder(std::make_shared<Order>(OrderType::Limit, 1, Side::Buy, 99, 5));
    auto trades2 = ob.addorder(std::make_shared<Order>(OrderType::Limit, 2, Side::Sell, 101, 5));

    ASSERT_EQ(trades1.size(), 0u);
    ASSERT_EQ(trades2.size(), 0u);
    ASSERT_EQ(ob.ordercount(), 2u);
    ASSERT_TRUE(ob.hasorder(1));
    ASSERT_TRUE(ob.hasorder(2));

    auto agg = ob.getorderinfo();
    ASSERT_EQ(agg.getbids().size(), 1u);
    ASSERT_EQ(agg.getasks().size(), 1u);
    ASSERT_EQ(agg.getbids()[0].price, 99);
    ASSERT_EQ(agg.getbids()[0].quantity, 5);
    ASSERT_EQ(agg.getasks()[0].price, 101);
    ASSERT_EQ(agg.getasks()[0].quantity, 5);
}

// 3. Multiple orders at same price
TEST_CASE(Test03_MultipleOrdersAtSamePrice) {
    Orderbook ob;
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 1, Side::Buy, 100, 10));
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 2, Side::Buy, 100, 15));

    ASSERT_EQ(ob.ordercount(), 2u);
    ASSERT_EQ(ob.bidLevelCount(), 1u);

    auto agg = ob.getorderinfo();
    ASSERT_EQ(agg.getbids().size(), 1u);
    ASSERT_EQ(agg.getbids()[0].price, 100);
    ASSERT_EQ(agg.getbids()[0].quantity, 25);
}

// 4. FIFO execution at same price level
TEST_CASE(Test04_FIFOExecutionAtSamePrice) {
    Orderbook ob;
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 1, Side::Sell, 100, 5));
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 2, Side::Sell, 100, 10));

    auto trades = ob.addorder(std::make_shared<Order>(OrderType::Limit, 3, Side::Buy, 100, 7));

    ASSERT_EQ(trades.size(), 2u);
    // First trade must fill Order 1 (arrived first)
    ASSERT_EQ(trades[0].getSellOrderId(), 1);
    ASSERT_EQ(trades[0].getQuantity(), 5);

    // Second trade consumes remaining 2 from Order 2
    ASSERT_EQ(trades[1].getSellOrderId(), 2);
    ASSERT_EQ(trades[1].getQuantity(), 2);

    ASSERT_TRUE(!ob.hasorder(1));
    ASSERT_TRUE(ob.hasorder(2));
    ASSERT_EQ(ob.getorder(2)->getrem(), 8);
}

// 5. Multiple price levels
TEST_CASE(Test05_MultiplePriceLevels) {
    Orderbook ob;
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 1, Side::Sell, 101, 5));
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 2, Side::Sell, 102, 5));
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 3, Side::Sell, 103, 5));

    auto trades = ob.addorder(std::make_shared<Order>(OrderType::Limit, 4, Side::Buy, 103, 12));

    ASSERT_EQ(trades.size(), 3u);
    ASSERT_EQ(trades[0].getPrice(), 101);
    ASSERT_EQ(trades[0].getQuantity(), 5);
    ASSERT_EQ(trades[1].getPrice(), 102);
    ASSERT_EQ(trades[1].getQuantity(), 5);
    ASSERT_EQ(trades[2].getPrice(), 103);
    ASSERT_EQ(trades[2].getQuantity(), 2);

    ASSERT_TRUE(!ob.hasorder(1));
    ASSERT_TRUE(!ob.hasorder(2));
    ASSERT_TRUE(ob.hasorder(3));
    ASSERT_EQ(ob.getorder(3)->getrem(), 3);
}

/* -----------------------------------------------------
   MARKET ORDER TESTS (6 - 10)
   ----------------------------------------------------- */

// 6. Market Buy against multiple ask levels
TEST_CASE(Test06_MarketBuyMultipleLevels) {
    Orderbook ob;
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 1, Side::Sell, 100, 5));
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 2, Side::Sell, 101, 5));
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 3, Side::Sell, 102, 5));

    auto trades = ob.addorder(std::make_shared<Order>(4, Side::Buy, 12));

    ASSERT_EQ(trades.size(), 3u);
    ASSERT_EQ(trades[0].getPrice(), 100);
    ASSERT_EQ(trades[0].getQuantity(), 5);
    ASSERT_EQ(trades[1].getPrice(), 101);
    ASSERT_EQ(trades[1].getQuantity(), 5);
    ASSERT_EQ(trades[2].getPrice(), 102);
    ASSERT_EQ(trades[2].getQuantity(), 2);

    ASSERT_TRUE(!ob.hasorder(4)); // Market order never rests!
    ASSERT_EQ(ob.getorder(3)->getrem(), 3);
}

// 7. Market Sell against multiple bid levels
TEST_CASE(Test07_MarketSellMultipleLevels) {
    Orderbook ob;
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 1, Side::Buy, 100, 5));
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 2, Side::Buy, 99, 5));
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 3, Side::Buy, 98, 5));

    auto trades = ob.addorder(std::make_shared<Order>(4, Side::Sell, 12));

    ASSERT_EQ(trades.size(), 3u);
    ASSERT_EQ(trades[0].getPrice(), 100);
    ASSERT_EQ(trades[0].getQuantity(), 5);
    ASSERT_EQ(trades[1].getPrice(), 99);
    ASSERT_EQ(trades[1].getQuantity(), 5);
    ASSERT_EQ(trades[2].getPrice(), 98);
    ASSERT_EQ(trades[2].getQuantity(), 2);

    ASSERT_TRUE(!ob.hasorder(4)); // Market order never rests!
    ASSERT_EQ(ob.getorder(3)->getrem(), 3);
}

// 8. Market order with insufficient liquidity
TEST_CASE(Test08_MarketOrderInsufficientLiquidity) {
    Orderbook ob;
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 1, Side::Sell, 100, 5));

    auto trades = ob.addorder(std::make_shared<Order>(2, Side::Buy, 15));

    ASSERT_EQ(trades.size(), 1u);
    ASSERT_EQ(trades[0].getQuantity(), 5);
    ASSERT_EQ(ob.ordercount(), 0u);
    ASSERT_TRUE(!ob.hasorder(2));
}

// 9. Market order when opposite book is empty
TEST_CASE(Test09_MarketOrderOppositeBookEmpty) {
    Orderbook ob;
    auto trades = ob.addorder(std::make_shared<Order>(1, Side::Buy, 10));

    ASSERT_EQ(trades.size(), 0u);
    ASSERT_EQ(ob.ordercount(), 0u);
    ASSERT_TRUE(!ob.hasorder(1));
}

// 10. Verify market orders never remain in the book
TEST_CASE(Test10_MarketOrdersNeverRest) {
    Orderbook ob;
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 1, Side::Sell, 100, 5));
    ob.addorder(std::make_shared<Order>(2, Side::Buy, 10)); // partially fills
    ob.addorder(std::make_shared<Order>(3, Side::Sell, 10)); // no bids

    ASSERT_TRUE(!ob.hasorder(2));
    ASSERT_TRUE(!ob.hasorder(3));
    ASSERT_EQ(ob.ordercount(), 0u);
    auto agg = ob.getorderinfo();
    ASSERT_EQ(agg.getbids().size(), 0u);
    ASSERT_EQ(agg.getasks().size(), 0u);
}

/* -----------------------------------------------------
   FILL-AND-KILL (FAK) TESTS (11 - 14)
   ----------------------------------------------------- */

// 11. FAK fully filled
TEST_CASE(Test11_FAKFullyFilled) {
    Orderbook ob;
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 1, Side::Sell, 100, 10));
    auto trades = ob.addorder(std::make_shared<Order>(OrderType::FillAndKill, 2, Side::Buy, 100, 10));

    ASSERT_EQ(trades.size(), 1u);
    ASSERT_EQ(trades[0].getQuantity(), 10);
    ASSERT_EQ(ob.ordercount(), 0u);
    ASSERT_TRUE(!ob.hasorder(2));
}

// 12. FAK partially filled
TEST_CASE(Test12_FAKPartiallyFilled) {
    Orderbook ob;
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 1, Side::Sell, 100, 5));
    auto trades = ob.addorder(std::make_shared<Order>(OrderType::FillAndKill, 2, Side::Buy, 100, 10));

    ASSERT_EQ(trades.size(), 1u);
    ASSERT_EQ(trades[0].getQuantity(), 5);
    ASSERT_EQ(ob.ordercount(), 0u);
    ASSERT_TRUE(!ob.hasorder(2)); // Remaining 5 was cancelled immediately
}

// 13. FAK with no liquidity / non-crossing limit
TEST_CASE(Test13_FAKNoLiquidity) {
    Orderbook ob;
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 1, Side::Sell, 105, 5));
    auto trades = ob.addorder(std::make_shared<Order>(OrderType::FillAndKill, 2, Side::Buy, 100, 10));

    ASSERT_EQ(trades.size(), 0u);
    ASSERT_EQ(ob.ordercount(), 1u);
    ASSERT_TRUE(ob.hasorder(1));
    ASSERT_TRUE(!ob.hasorder(2)); // Never enters book
}

// 14. Verify FAK remainder never rests
TEST_CASE(Test14_FAKRemainderNeverRests) {
    Orderbook ob;
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 1, Side::Buy, 100, 5));
    auto trades = ob.addorder(std::make_shared<Order>(OrderType::FillAndKill, 2, Side::Sell, 95, 20));

    ASSERT_EQ(trades.size(), 1u);
    ASSERT_EQ(trades[0].getQuantity(), 5);
    ASSERT_EQ(ob.ordercount(), 0u);
    ASSERT_TRUE(!ob.hasorder(2));
    auto agg = ob.getorderinfo();
    ASSERT_EQ(agg.getbids().size(), 0u);
    ASSERT_EQ(agg.getasks().size(), 0u);
}

/* -----------------------------------------------------
   CANCELLATION TESTS (15 - 17)
   ----------------------------------------------------- */

// 15. Cancel existing order
TEST_CASE(Test15_CancelExistingOrder) {
    Orderbook ob;
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 1, Side::Buy, 100, 10));
    ASSERT_TRUE(ob.hasorder(1));

    ob.cancelorder(1);
    ASSERT_TRUE(!ob.hasorder(1));
    ASSERT_EQ(ob.ordercount(), 0u);
    ASSERT_EQ(ob.bidLevelCount(), 0u);
}

// 16. Cancel unknown order throws exception
TEST_CASE(Test16_CancelUnknownOrder) {
    Orderbook ob;
    ASSERT_THROWS(ob.cancelorder(999), std::out_of_range);
}

// 17. Cancel fully filled order throws exception
TEST_CASE(Test17_CancelFullyFilledOrder) {
    Orderbook ob;
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 1, Side::Sell, 100, 5));
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 2, Side::Buy, 100, 5));

    ASSERT_TRUE(!ob.hasorder(1));
    ASSERT_THROWS(ob.cancelorder(1), std::out_of_range);
}

/* -----------------------------------------------------
   MODIFICATION TESTS (18 - 20)
   ----------------------------------------------------- */

// 18. Reduce quantity while preserving priority
TEST_CASE(Test18_ReduceQuantityPreservesPriority) {
    Orderbook ob;
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 1, Side::Buy, 100, 10));
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 2, Side::Buy, 100, 10));

    // Reduce Order 1 quantity to 4 (same price)
    ob.Matchorder(OrderModify(1, Side::Buy, 100, 4));
    ASSERT_EQ(ob.getorder(1)->getrem(), 4);

    // Incoming Sell for 6
    auto trades = ob.addorder(std::make_shared<Order>(OrderType::Limit, 3, Side::Sell, 100, 6));

    ASSERT_EQ(trades.size(), 2u);
    // Order 1 was first in queue, so it fills all 4
    ASSERT_EQ(trades[0].getBuyOrderId(), 1);
    ASSERT_EQ(trades[0].getQuantity(), 4);
    // Order 2 fills remainder 2
    ASSERT_EQ(trades[1].getBuyOrderId(), 2);
    ASSERT_EQ(trades[1].getQuantity(), 2);
}

// 19. Modify price loses priority (receives new priority)
TEST_CASE(Test19_ModifyPriceLosesPriority) {
    Orderbook ob;
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 1, Side::Buy, 100, 10));
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 2, Side::Buy, 99, 10));

    // Order 1 changes price from 100 to 99: it should go BEHIND Order 2 at price 99
    ob.Matchorder(OrderModify(1, Side::Buy, 99, 10));

    auto trades = ob.addorder(std::make_shared<Order>(OrderType::Limit, 3, Side::Sell, 99, 15));

    ASSERT_EQ(trades.size(), 2u);
    // Order 2 must fill first because Order 1 lost priority
    ASSERT_EQ(trades[0].getBuyOrderId(), 2);
    ASSERT_EQ(trades[0].getQuantity(), 10);
    // Order 1 fills remaining 5
    ASSERT_EQ(trades[1].getBuyOrderId(), 1);
    ASSERT_EQ(trades[1].getQuantity(), 5);
}

// 20. Invalid modification
TEST_CASE(Test20_InvalidModification) {
    Orderbook ob;
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 1, Side::Buy, 100, 10));

    // Non-existent order
    ASSERT_THROWS(ob.Matchorder(OrderModify(999, Side::Buy, 100, 5)), std::out_of_range);

    // Zero quantity
    ASSERT_THROWS(OrderModify(1, Side::Buy, 100, 0), std::invalid_argument);

    // Negative price
    ASSERT_THROWS(OrderModify(1, Side::Buy, -5, 10), std::invalid_argument);

    // Side mismatch
    ASSERT_THROWS(ob.Matchorder(OrderModify(1, Side::Sell, 100, 5)), std::invalid_argument);
}

/* -----------------------------------------------------
   INPUT VALIDATION TESTS (21 - 25)
   ----------------------------------------------------- */

// 21. Duplicate order ID
TEST_CASE(Test21_DuplicateOrderId) {
    Orderbook ob;
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 1, Side::Buy, 100, 10));
    ASSERT_THROWS(ob.addorder(std::make_shared<Order>(OrderType::Limit, 1, Side::Sell, 105, 5)), std::invalid_argument);
}

// 22. Zero quantity
TEST_CASE(Test22_ZeroQuantity) {
    ASSERT_THROWS(Order(OrderType::Limit, 1, Side::Buy, 100, 0), std::invalid_argument);
}

// 23. Negative quantity
TEST_CASE(Test23_NegativeQuantity) {
    ASSERT_THROWS(Order(OrderType::Limit, 1, Side::Buy, 100, -10), std::invalid_argument);
}

// 24. Invalid limit price
TEST_CASE(Test24_InvalidLimitPrice) {
    ASSERT_THROWS(Order(OrderType::Limit, 1, Side::Buy, 0, 10), std::invalid_argument);
    ASSERT_THROWS(Order(OrderType::Limit, 1, Side::Buy, -50, 10), std::invalid_argument);
    ASSERT_THROWS(Order(OrderType::FillAndKill, 2, Side::Sell, -1, 10), std::invalid_argument);
}

// 25. Invalid order (null pointer)
TEST_CASE(Test25_NullOrder) {
    Orderbook ob;
    ASSERT_THROWS(ob.addorder(nullptr), std::invalid_argument);
}

/* -----------------------------------------------------
   MATCHING CORRECTNESS & INVARIANTS TESTS (26 - 30)
   ----------------------------------------------------- */

// 26. Verify execution price equals resting order price
TEST_CASE(Test26_ExecutionPriceEqualsRestingPrice) {
    Orderbook ob;
    // Scenario A: Resting Sell at 100. Aggressive Buy limit at 105.
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 1, Side::Sell, 100, 10));
    auto tradesA = ob.addorder(std::make_shared<Order>(OrderType::Limit, 2, Side::Buy, 105, 5));
    ASSERT_EQ(tradesA.size(), 1u);
    ASSERT_EQ(tradesA[0].getPrice(), 100); // Resting price! Not 105.

    // Scenario B: Resting Buy at 95. Aggressive Sell limit at 90.
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 3, Side::Buy, 95, 10));
    auto tradesB = ob.addorder(std::make_shared<Order>(OrderType::Limit, 4, Side::Sell, 90, 5));
    ASSERT_EQ(tradesB.size(), 1u);
    ASSERT_EQ(tradesB[0].getPrice(), 95); // Resting price! Not 90.
}

// 27. Verify correct trade quantity
TEST_CASE(Test27_CorrectTradeQuantity) {
    Orderbook ob;
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 1, Side::Sell, 100, 10));

    auto t1 = ob.addorder(std::make_shared<Order>(OrderType::Limit, 2, Side::Buy, 100, 4));
    ASSERT_EQ(t1.size(), 1u);
    ASSERT_EQ(t1[0].getQuantity(), 4);

    auto t2 = ob.addorder(std::make_shared<Order>(OrderType::Limit, 3, Side::Buy, 100, 6));
    ASSERT_EQ(t2.size(), 1u);
    ASSERT_EQ(t2[0].getQuantity(), 6);

    ASSERT_EQ(ob.ordercount(), 0u);
}

// 28. Verify order-book aggregation after trades
TEST_CASE(Test28_OrderbookAggregationAfterTrades) {
    Orderbook ob;
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 1, Side::Buy, 99, 10));
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 2, Side::Buy, 99, 5));
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 3, Side::Buy, 98, 20));
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 4, Side::Sell, 101, 15));
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 5, Side::Sell, 102, 10));

    auto agg1 = ob.getorderinfo();
    ASSERT_EQ(agg1.getbids().size(), 2u);
    ASSERT_EQ(agg1.getbids()[0].price, 99);
    ASSERT_EQ(agg1.getbids()[0].quantity, 15);
    ASSERT_EQ(agg1.getbids()[1].price, 98);
    ASSERT_EQ(agg1.getbids()[1].quantity, 20);
    ASSERT_EQ(agg1.getasks().size(), 2u);
    ASSERT_EQ(agg1.getasks()[0].price, 101);
    ASSERT_EQ(agg1.getasks()[0].quantity, 15);

    // Consume 12 from bids at 99
    ob.addorder(std::make_shared<Order>(6, Side::Sell, 12));

    auto agg2 = ob.getorderinfo();
    ASSERT_EQ(agg2.getbids().size(), 2u);
    ASSERT_EQ(agg2.getbids()[0].price, 99);
    ASSERT_EQ(agg2.getbids()[0].quantity, 3); // 15 - 12 = 3 remaining
}

// 29. Verify no stale orders remain in lookup map
TEST_CASE(Test29_NoStaleOrdersInLookupMap) {
    Orderbook ob;
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 1, Side::Sell, 100, 10));
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 2, Side::Sell, 101, 10));

    // Fully consume Order 1 with market buy
    ob.addorder(std::make_shared<Order>(3, Side::Buy, 10));

    ASSERT_TRUE(!ob.hasorder(1));
    ASSERT_TRUE(!ob.hasorder(3)); // Market order
    ASSERT_TRUE(ob.hasorder(2));
    ASSERT_EQ(ob.ordercount(), 1u);
}

// 30. Verify no empty price levels remain
TEST_CASE(Test30_NoEmptyPriceLevelsRemain) {
    Orderbook ob;
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 1, Side::Sell, 100, 5));
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 2, Side::Sell, 101, 5));
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 3, Side::Sell, 102, 5));

    ASSERT_EQ(ob.askLevelCount(), 3u);

    // Fully consume price 100 and 101
    ob.addorder(std::make_shared<Order>(4, Side::Buy, 10));

    ASSERT_TRUE(!ob.hasAskLevel(100));
    ASSERT_TRUE(!ob.hasAskLevel(101));
    ASSERT_TRUE(ob.hasAskLevel(102));
    ASSERT_EQ(ob.askLevelCount(), 1u);
}

/* -----------------------------------------------------
   ADDITIONAL EDGE-CASE TESTS (31 - 35)
   ----------------------------------------------------- */

// 31. Modify quantity increase loses time priority
TEST_CASE(Test31_QuantityIncreaseLosesPriority) {
    Orderbook ob;
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 1, Side::Buy, 100, 5));
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 2, Side::Buy, 100, 5));

    // Increase Order 1 quantity: it should lose priority and move behind Order 2
    ob.Matchorder(OrderModify(1, Side::Buy, 100, 10));

    // Incoming Sell for 8
    auto trades = ob.addorder(std::make_shared<Order>(OrderType::Limit, 3, Side::Sell, 100, 8));

    ASSERT_EQ(trades.size(), 2u);
    // Order 2 must fill first because Order 1 forfeited priority on size increase
    ASSERT_EQ(trades[0].getBuyOrderId(), 2);
    ASSERT_EQ(trades[0].getQuantity(), 5);
    // Order 1 fills remaining 3
    ASSERT_EQ(trades[1].getBuyOrderId(), 1);
    ASSERT_EQ(trades[1].getQuantity(), 3);
    ASSERT_EQ(ob.getorder(1)->getrem(), 7);
}

// 32. FAK sweeping multiple price levels up to its limit price
TEST_CASE(Test32_FAKSweepMultipleLevelsUpToLimit) {
    Orderbook ob;
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 1, Side::Sell, 100, 5));
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 2, Side::Sell, 101, 5));
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 3, Side::Sell, 105, 10));

    // FAK Buy with limit price 102 for quantity 15:
    // Should consume 5 @ 100, 5 @ 101, and cancel remaining 5 because 105 > 102
    auto trades = ob.addorder(std::make_shared<Order>(OrderType::FillAndKill, 4, Side::Buy, 102, 15));

    ASSERT_EQ(trades.size(), 2u);
    ASSERT_EQ(trades[0].getPrice(), 100);
    ASSERT_EQ(trades[0].getQuantity(), 5);
    ASSERT_EQ(trades[1].getPrice(), 101);
    ASSERT_EQ(trades[1].getQuantity(), 5);

    ASSERT_TRUE(!ob.hasorder(4)); // FAK never rests
    ASSERT_TRUE(ob.hasorder(3));  // Ask @ 105 is untouched
    ASSERT_EQ(ob.getorder(3)->getrem(), 10);
}

// 33. Overfill error throws proper std::runtime_error
TEST_CASE(Test33_OverfillException) {
    Order order(OrderType::Limit, 1, Side::Buy, 100, 10);
    order.fill(6);
    ASSERT_EQ(order.getrem(), 4);
    // Attempting to fill 5 when only 4 remain must throw std::runtime_error
    ASSERT_THROWS(order.fill(5), std::runtime_error);
}

// 34. Empty orderbook aggregation returns empty vectors
TEST_CASE(Test34_EmptyBookAggregation) {
    Orderbook ob;
    auto agg = ob.getorderinfo();
    ASSERT_TRUE(agg.getbids().empty());
    ASSERT_TRUE(agg.getasks().empty());
}

// 35. Modify with identical price and quantity is safe no-op
TEST_CASE(Test35_ModifyNoOpIdenticalParams) {
    Orderbook ob;
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 1, Side::Buy, 100, 10));
    ob.addorder(std::make_shared<Order>(OrderType::Limit, 2, Side::Buy, 100, 10));

    // Same price, same quantity: preserves priority
    auto trades = ob.Matchorder(OrderModify(1, Side::Buy, 100, 10));
    ASSERT_EQ(trades.size(), 0u);

    auto matchTrades = ob.addorder(std::make_shared<Order>(OrderType::Limit, 3, Side::Sell, 100, 5));
    ASSERT_EQ(matchTrades.size(), 1u);
    ASSERT_EQ(matchTrades[0].getBuyOrderId(), 1); // Order 1 still had priority
}


/* -----------------------------------------------------
   Main Test Runner
   ----------------------------------------------------- */

int main() {
    std::cout << "=========================================================\n";
    std::cout << "  Orderbook Matching Engine Test Suite (Phase 1)\n";
    std::cout << "=========================================================\n";
    std::cout << "Total Tests Executed: " << g_totalTests << "\n";
    std::cout << "Passed: " << g_passedTests << "\n";
    std::cout << "Failed: " << g_failedTests << "\n";
    std::cout << "=========================================================\n";

    if (g_failedTests == 0) {
        std::cout << "ALL " << g_passedTests << " TESTS PASSED SUCCESSFULLY!\n";
        return 0;
    } else {
        std::cerr << "FAILURES DETECTED: " << g_failedTests << " tests failed!\n";
        return 1;
    }
}
