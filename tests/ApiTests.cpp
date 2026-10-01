#include "ApiServer.h"
#include "ApiJson.h"
#include "FixApp.h"
#include "HttpApiServer.h"
#include "WebSocketBroadcaster.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#endif

static std::string sendHttpRequest(unsigned short port, const std::string& request) {
    ApiSocket socketHandle = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socketHandle == INVALID_SOCKET) {
        throw std::runtime_error("client socket creation failed");
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (::connect(socketHandle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
#ifdef _WIN32
        closesocket(socketHandle);
#else
        close(socketHandle);
#endif
        throw std::runtime_error("client connect failed");
    }
    size_t sentTotal = 0;
    while (sentTotal < request.size()) {
        int sent = send(socketHandle, request.data() + sentTotal, static_cast<int>(request.size() - sentTotal), 0);
        if (sent <= 0) {
#ifdef _WIN32
            closesocket(socketHandle);
#else
            close(socketHandle);
#endif
            throw std::runtime_error("client send failed");
        }
        sentTotal += static_cast<size_t>(sent);
    }
    std::string response;
    char buffer[4096];
    while (true) {
        int received = recv(socketHandle, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            break;
        }
        response.append(buffer, buffer + received);
        const size_t headerEnd = response.find("\r\n\r\n");
        if (headerEnd != std::string::npos) {
            if (response.find("HTTP/1.1 101") != std::string::npos) {
                break;
            }
            const std::string header = response.substr(0, headerEnd);
            const size_t contentLengthPos = header.find("Content-Length: ");
            if (contentLengthPos != std::string::npos) {
                const size_t valueStart = contentLengthPos + std::string("Content-Length: ").size();
                const size_t valueEnd = header.find("\r\n", valueStart);
                const size_t contentLength = static_cast<size_t>(std::stoul(header.substr(valueStart, valueEnd - valueStart)));
                if (response.size() >= headerEnd + 4 + contentLength) {
                    break;
                }
            }
        }
    }
#ifdef _WIN32
    closesocket(socketHandle);
#else
    close(socketHandle);
#endif
    return response;
}

static int g_apiTotalTests = 0;
static int g_apiPassedTests = 0;
static int g_apiFailedTests = 0;

#define TEST_CASE(name) \
    void name(); \
    struct Register_##name { \
        Register_##name() { \
            g_apiTotalTests++; \
            try { \
                std::cout << "[RUN ] " << #name << std::endl; \
                name(); \
                g_apiPassedTests++; \
                std::cout << "[PASS] " << #name << "\n" << std::endl; \
            } catch (const std::exception& e) { \
                g_apiFailedTests++; \
                std::cerr << "[FAIL] " << #name << " - Exception: " << e.what() << "\n" << std::endl; \
            } catch (...) { \
                g_apiFailedTests++; \
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

TEST_CASE(TestA01_RestLimitOrderQueuesThroughEngine) {
    MatchingEngine engine;
    ApiServer api(engine);
    engine.start();

    auto response = api.submitOrder(ApiNewOrderRequest{"REST-1", "AAPL", Side::Buy, OrderType::Limit, 100, 10});
    ASSERT_TRUE(response.success);
    ASSERT_TRUE(response.orderId >= 1000000);
    auto book = api.getOrderbook();
    ASSERT_EQ(book.bids.size(), 1u);
    ASSERT_EQ(book.bids[0].quantity, 10);

    engine.stop(true);
}

TEST_CASE(TestA02_RestMarketOrderGeneratesTrade) {
    MatchingEngine engine;
    ApiServer api(engine);
    engine.start();

    api.submitOrder(ApiNewOrderRequest{"ASK", "AAPL", Side::Sell, OrderType::Limit, 100, 10});
    auto response = api.submitOrder(ApiNewOrderRequest{"MKT", "AAPL", Side::Buy, OrderType::Market, 0, 4});

    ASSERT_TRUE(response.success);
    ASSERT_EQ(response.executedTrades.size(), 1u);
    ASSERT_EQ(response.executedTrades[0].getPrice(), 100);
    ASSERT_EQ(api.getRecentTrades().size(), 1u);

    engine.stop(true);
}

TEST_CASE(TestA03_RestFakCancelsRemainder) {
    MatchingEngine engine;
    ApiServer api(engine);
    engine.start();

    api.submitOrder(ApiNewOrderRequest{"ASK", "AAPL", Side::Sell, OrderType::Limit, 100, 5});
    auto response = api.submitOrder(ApiNewOrderRequest{"FAK", "AAPL", Side::Buy, OrderType::FillAndKill, 100, 10});

    ASSERT_TRUE(response.success);
    auto status = api.getOrderStatus("FAK");
    ASSERT_TRUE(status.has_value());
    ASSERT_EQ(status->filledQuantity, 5);
    ASSERT_EQ(status->remainingQuantity, 0);
    ASSERT_EQ(status->status, ApiOrderStatusType::Canceled);

    engine.stop(true);
}

TEST_CASE(TestA04_RestCancelSucceedsThroughQueue) {
    MatchingEngine engine;
    ApiServer api(engine);
    engine.start();

    auto accepted = api.submitOrder(ApiNewOrderRequest{"CANCEL-ME", "AAPL", Side::Buy, OrderType::Limit, 99, 10});
    auto canceled = api.cancelOrder(ApiCancelOrderRequest{accepted.orderId, {}, Side::Buy});

    ASSERT_TRUE(canceled.success);
    auto status = api.getOrderStatus(accepted.orderId);
    ASSERT_TRUE(status.has_value());
    ASSERT_EQ(status->status, ApiOrderStatusType::Canceled);
    ASSERT_EQ(status->remainingQuantity, 0);

    engine.stop(true);
}

TEST_CASE(TestA05_RestCancelUnknownFails) {
    MatchingEngine engine;
    ApiServer api(engine);
    engine.start();

    auto response = api.cancelOrder(ApiCancelOrderRequest{999, {}, Side::Buy});
    ASSERT_TRUE(!response.success);
    ASSERT_EQ(response.httpStatus, 404);

    engine.stop(true);
}

TEST_CASE(TestA06_RestModifySucceedsThroughQueue) {
    MatchingEngine engine;
    ApiServer api(engine);
    engine.start();

    auto accepted = api.submitOrder(ApiNewOrderRequest{"MOD", "AAPL", Side::Buy, OrderType::Limit, 99, 10});
    auto modified = api.modifyOrder(ApiModifyOrderRequest{accepted.orderId, "MOD-2", Side::Buy, 99, 4});

    ASSERT_TRUE(modified.success);
    auto status = api.getOrderStatus("MOD-2");
    ASSERT_TRUE(status.has_value());
    ASSERT_EQ(status->remainingQuantity, 4);
    ASSERT_EQ(status->status, ApiOrderStatusType::Replaced);

    engine.stop(true);
}

TEST_CASE(TestA07_RestModifyPrioritySemantics) {
    MatchingEngine engine;
    ApiServer api(engine);
    engine.start();

    auto first = api.submitOrder(ApiNewOrderRequest{"B1", "AAPL", Side::Buy, OrderType::Limit, 100, 5});
    api.submitOrder(ApiNewOrderRequest{"B2", "AAPL", Side::Buy, OrderType::Limit, 100, 5});
    api.modifyOrder(ApiModifyOrderRequest{first.orderId, "B1-R", Side::Buy, 100, 10});
    api.submitOrder(ApiNewOrderRequest{"S1", "AAPL", Side::Sell, OrderType::Limit, 100, 6});

    auto b2 = api.getOrderStatus("B2");
    auto b1r = api.getOrderStatus("B1-R");
    ASSERT_TRUE(b2.has_value());
    ASSERT_TRUE(b1r.has_value());
    ASSERT_EQ(b2->filledQuantity, 5);
    ASSERT_EQ(b1r->filledQuantity, 1);

    engine.stop(true);
}

TEST_CASE(TestA08_RestOrderbookReadsImmutableSnapshot) {
    MatchingEngine engine;
    ApiServer api(engine);
    engine.start();

    api.submitOrder(ApiNewOrderRequest{"BID", "AAPL", Side::Buy, OrderType::Limit, 101, 7});
    auto book = api.getOrderbook();
    ASSERT_EQ(book.bids.size(), 1u);
    ASSERT_EQ(book.bids[0].price, 101);
    ASSERT_EQ(book.bids[0].quantity, 7);

    engine.stop(true);
}

TEST_CASE(TestA09_RestRecentTradesFromListener) {
    MatchingEngine engine;
    ApiServer api(engine);
    engine.start();

    api.submitOrder(ApiNewOrderRequest{"S", "AAPL", Side::Sell, OrderType::Limit, 101, 3});
    api.submitOrder(ApiNewOrderRequest{"B", "AAPL", Side::Buy, OrderType::Limit, 101, 3});

    auto trades = api.getRecentTrades();
    ASSERT_EQ(trades.size(), 1u);
    ASSERT_EQ(trades[0].price, 101);
    ASSERT_EQ(trades[0].quantity, 3);

    engine.stop(true);
}

TEST_CASE(TestA10_OrderbookBroadcastUsesSnapshot) {
    MatchingEngine engine;
    ApiServer api(engine);
    engine.start();

    api.submitOrder(ApiNewOrderRequest{"B", "AAPL", Side::Buy, OrderType::Limit, 101, 3});

    bool sawOrderbook = false;
    while (auto msg = api.outboundQueue().tryPop()) {
        sawOrderbook = sawOrderbook || msg->type == OutboundMessage::Type::Orderbook;
    }
    ASSERT_TRUE(sawOrderbook);

    engine.stop(true);
}

TEST_CASE(TestA11_TradeBroadcastUsesTradeListener) {
    MatchingEngine engine;
    ApiServer api(engine);
    engine.start();

    api.submitOrder(ApiNewOrderRequest{"S", "AAPL", Side::Sell, OrderType::Limit, 101, 3});
    api.submitOrder(ApiNewOrderRequest{"B", "AAPL", Side::Buy, OrderType::Limit, 101, 3});

    bool sawTrade = false;
    while (auto msg = api.outboundQueue().tryPop()) {
        sawTrade = sawTrade || msg->type == OutboundMessage::Type::Trade;
    }
    ASSERT_TRUE(sawTrade);

    engine.stop(true);
}

TEST_CASE(TestA12_OrderUpdateBroadcastUsesEventListener) {
    MatchingEngine engine;
    ApiServer api(engine);
    engine.start();

    api.submitOrder(ApiNewOrderRequest{"B", "AAPL", Side::Buy, OrderType::Limit, 101, 3});

    bool sawOrderUpdate = false;
    while (auto msg = api.outboundQueue().tryPop()) {
        sawOrderUpdate = sawOrderUpdate || msg->type == OutboundMessage::Type::OrderUpdate;
    }
    ASSERT_TRUE(sawOrderUpdate);

    engine.stop(true);
}

TEST_CASE(TestA13_FullOutboundQueueDoesNotBlockMatching) {
    MatchingEngine engine;
    ApiServer api(engine, 1000, 2);
    engine.start();

    for (int i = 0; i < 20; ++i) {
        auto response = api.submitOrder(ApiNewOrderRequest{"B" + std::to_string(i), "AAPL", Side::Buy, OrderType::Limit, 100 + i, 1});
        ASSERT_TRUE(response.success);
    }

    ASSERT_TRUE(api.droppedBroadcastCount() > 0);
    ASSERT_TRUE(engine.getLastProcessedSequence() >= 20);

    engine.stop(true);
}

TEST_CASE(TestA14_InvalidRestRequestRejectedBeforeEngine) {
    MatchingEngine engine;
    ApiServer api(engine);
    engine.start();

    auto before = engine.getLastProcessedSequence();
    auto response = api.submitOrder(ApiNewOrderRequest{"BAD", "AAPL", Side::Buy, OrderType::Limit, 0, 10});

    ASSERT_TRUE(!response.success);
    ASSERT_EQ(response.httpStatus, 400);
    ASSERT_EQ(engine.getLastProcessedSequence(), before);

    engine.stop(true);
}

TEST_CASE(TestA15_RestDoesNotMutateBeforeQueuedResult) {
    MatchingEngine engine;
    ApiServer api(engine);
    engine.start();

    auto response = api.submitOrder(ApiNewOrderRequest{"QUEUE", "AAPL", Side::Buy, OrderType::Limit, 100, 2});
    ASSERT_TRUE(response.success);
    ASSERT_TRUE(response.sequenceNumber > 0);
    ASSERT_TRUE(engine.getLastProcessedSequence() >= response.sequenceNumber);

    engine.stop(true);
}

TEST_CASE(TestA16_FixAndRestLimitSemanticsIdentical) {
    MatchingEngine fixEngine;
    FixApp fix(fixEngine);
    fixEngine.start();
    fixEngine.waitForSequence(fix.submitNewOrder(FixNewOrderRequest{"FS", "AAPL", Side::Sell, OrderType::Limit, 10, 100}));
    fixEngine.waitForSequence(fix.submitNewOrder(FixNewOrderRequest{"FB", "AAPL", Side::Buy, OrderType::Limit, 4, 100}));

    MatchingEngine restEngine;
    ApiServer api(restEngine);
    restEngine.start();
    api.submitOrder(ApiNewOrderRequest{"RS", "AAPL", Side::Sell, OrderType::Limit, 100, 10});
    api.submitOrder(ApiNewOrderRequest{"RB", "AAPL", Side::Buy, OrderType::Limit, 100, 4});

    auto fixReports = fix.getReports();
    auto restTrades = api.getRecentTrades();
    ASSERT_EQ(restTrades.size(), 1u);
    ASSERT_EQ(restTrades[0].price, 100);
    ASSERT_EQ(restTrades[0].quantity, 4);
    bool fixSawSameFill = false;
    for (const auto& report : fixReports) {
        fixSawSameFill = fixSawSameFill || (report.lastPx == 100 && report.lastQty == 4);
    }
    ASSERT_TRUE(fixSawSameFill);

    fixEngine.stop(true);
    restEngine.stop(true);
}

TEST_CASE(TestA17_FixAndRestMarketSemanticsIdentical) {
    MatchingEngine fixEngine;
    FixApp fix(fixEngine);
    fixEngine.start();
    fixEngine.waitForSequence(fix.submitNewOrder(FixNewOrderRequest{"FS1", "AAPL", Side::Sell, OrderType::Limit, 5, 100}));
    fixEngine.waitForSequence(fix.submitNewOrder(FixNewOrderRequest{"FM1", "AAPL", Side::Buy, OrderType::Market, 7, 0}));

    MatchingEngine restEngine;
    ApiServer api(restEngine);
    restEngine.start();
    api.submitOrder(ApiNewOrderRequest{"RS1", "AAPL", Side::Sell, OrderType::Limit, 100, 5});
    api.submitOrder(ApiNewOrderRequest{"RM1", "AAPL", Side::Buy, OrderType::Market, 0, 7});

    auto restTrades = api.getRecentTrades();
    ASSERT_EQ(restTrades.size(), 1u);
    ASSERT_EQ(restTrades[0].price, 100);
    ASSERT_EQ(restTrades[0].quantity, 5);
    bool fixSawSameFill = false;
    for (const auto& report : fix.getReports()) {
        fixSawSameFill = fixSawSameFill || (report.clOrdId == "FM1" && report.lastPx == 100 && report.lastQty == 5);
    }
    ASSERT_TRUE(fixSawSameFill);

    fixEngine.stop(true);
    restEngine.stop(true);
}

TEST_CASE(TestA18_FixAndRestFakSemanticsIdentical) {
    MatchingEngine fixEngine;
    FixApp fix(fixEngine);
    fixEngine.start();
    fixEngine.waitForSequence(fix.submitNewOrder(FixNewOrderRequest{"FS2", "AAPL", Side::Sell, OrderType::Limit, 5, 100}));
    fixEngine.waitForSequence(fix.submitNewOrder(FixNewOrderRequest{"FFAK", "AAPL", Side::Buy, OrderType::FillAndKill, 7, 100}));

    MatchingEngine restEngine;
    ApiServer api(restEngine);
    restEngine.start();
    api.submitOrder(ApiNewOrderRequest{"RS2", "AAPL", Side::Sell, OrderType::Limit, 100, 5});
    api.submitOrder(ApiNewOrderRequest{"RFAK", "AAPL", Side::Buy, OrderType::FillAndKill, 100, 7});

    auto restStatus = api.getOrderStatus("RFAK");
    ASSERT_TRUE(restStatus.has_value());
    ASSERT_EQ(restStatus->filledQuantity, 5);
    ASSERT_EQ(restStatus->remainingQuantity, 0);
    bool fixSawFillAndCancel = false;
    bool fixSawCancel = false;
    for (const auto& report : fix.getReports()) {
        fixSawFillAndCancel = fixSawFillAndCancel || (report.clOrdId == "FFAK" && report.lastQty == 5 && report.lastPx == 100);
        fixSawCancel = fixSawCancel || (report.clOrdId == "FFAK" && report.execType == FixExecType::Canceled);
    }
    ASSERT_TRUE(fixSawFillAndCancel);
    ASSERT_TRUE(fixSawCancel);

    fixEngine.stop(true);
    restEngine.stop(true);
}

TEST_CASE(TestA19_FailedModifyRestoresPreviousStatus) {
    MatchingEngine engine;
    ApiServer api(engine);
    engine.start();

    auto buy = api.submitOrder(ApiNewOrderRequest{"B", "AAPL", Side::Buy, OrderType::Limit, 100, 2});
    api.submitOrder(ApiNewOrderRequest{"S", "AAPL", Side::Sell, OrderType::Limit, 100, 2});
    auto response = api.modifyOrder(ApiModifyOrderRequest{buy.orderId, "B-REJECTED", Side::Buy, 101, 1});

    ASSERT_TRUE(!response.success);
    auto oldStatus = api.getOrderStatus("B");
    auto rejectedStatus = api.getOrderStatus("B-REJECTED");
    ASSERT_TRUE(oldStatus.has_value());
    ASSERT_EQ(oldStatus->status, ApiOrderStatusType::Filled);
    ASSERT_TRUE(!rejectedStatus.has_value());

    engine.stop(true);
}

TEST_CASE(TestA20_ModifiedClientOrderIdCanBeResolved) {
    MatchingEngine engine;
    ApiServer api(engine);
    engine.start();

    api.submitOrder(ApiNewOrderRequest{"ORIG", "AAPL", Side::Buy, OrderType::Limit, 100, 2});
    auto modified = api.modifyOrder(ApiModifyOrderRequest{0, "ORIG", Side::Buy, 100, 1});
    auto canceled = api.cancelOrder(ApiCancelOrderRequest{0, "ORIG", Side::Buy});

    ASSERT_TRUE(modified.success);
    ASSERT_TRUE(canceled.success);

    engine.stop(true);
}

TEST_CASE(TestA21_JsonNewOrderParsing) {
    std::string error;
    auto parsed = ApiJson::parseNewOrderRequest(
        "{\"clientOrderId\":\"J1\",\"symbol\":\"AAPL\",\"side\":\"BUY\",\"orderType\":\"LIMIT\",\"price\":101,\"quantity\":7}",
        error);

    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->clientOrderId, std::string("J1"));
    ASSERT_EQ(parsed->side, Side::Buy);
    ASSERT_EQ(parsed->orderType, OrderType::Limit);
    ASSERT_EQ(parsed->price, 101);
    ASSERT_EQ(parsed->quantity, 7);
}

TEST_CASE(TestA22_JsonResponseSerialization) {
    ApiResponse response;
    response.success = true;
    response.httpStatus = 201;
    response.message = "Order accepted";
    response.sequenceNumber = 42;
    response.orderId = 1000001;

    std::string json = ApiJson::serializeResponse(response);
    ASSERT_TRUE(json.find("\"success\":true") != std::string::npos);
    ASSERT_TRUE(json.find("\"httpStatus\":201") != std::string::npos);
    ASSERT_TRUE(json.find("\"orderId\":1000001") != std::string::npos);
}

TEST_CASE(TestA23_JsonOrderbookSerialization) {
    MatchingEngine engine;
    ApiServer api(engine);
    engine.start();

    api.submitOrder(ApiNewOrderRequest{"JSON-BID", "AAPL", Side::Buy, OrderType::Limit, 101, 7});
    std::string json = ApiJson::serializeOrderbook(api.getOrderbook());

    ASSERT_TRUE(json.find("\"bids\":[") != std::string::npos);
    ASSERT_TRUE(json.find("\"price\":101") != std::string::npos);
    ASSERT_TRUE(json.find("\"quantity\":7") != std::string::npos);

    engine.stop(true);
}

TEST_CASE(TestA24_WebSocketOrderbookPayloadContainsFullLevels) {
    MatchingEngine engine;
    ApiServer api(engine);
    engine.start();

    api.submitOrder(ApiNewOrderRequest{"WS-FULL", "AAPL", Side::Buy, OrderType::Limit, 101, 7});

    bool sawFullOrderbook = false;
    while (auto msg = api.outboundQueue().tryPop()) {
        if (msg->type == OutboundMessage::Type::Orderbook) {
            sawFullOrderbook = msg->payload.find("\"type\":\"orderbook\"") != std::string::npos &&
                msg->payload.find("\"sequenceNumber\":1") != std::string::npos &&
                msg->payload.find("\"bids\":[") != std::string::npos &&
                msg->payload.find("\"price\":101") != std::string::npos &&
                msg->payload.find("\"quantity\":7") != std::string::npos;
        }
    }
    ASSERT_TRUE(sawFullOrderbook);

    engine.stop(true);
}

TEST_CASE(TestA25_HttpHealthEndpoint) {
    MatchingEngine engine;
    ApiServer api(engine);
    HttpApiServer server(api, "127.0.0.1", 0);
    engine.start();
    server.start();

    std::string response = sendHttpRequest(server.port(), "GET /health HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
    ASSERT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    ASSERT_TRUE(response.find("\"status\":\"ok\"") != std::string::npos);

    server.stop();
    engine.stop(true);
}

TEST_CASE(TestA26_HttpSubmitOrderAndReadOrderbook) {
    MatchingEngine engine;
    ApiServer api(engine);
    HttpApiServer server(api, "127.0.0.1", 0);
    engine.start();
    server.start();

    const std::string body = "{\"clientOrderId\":\"HTTP-B1\",\"symbol\":\"AAPL\",\"side\":\"BUY\",\"orderType\":\"LIMIT\",\"price\":100,\"quantity\":5}";
    std::string post = "POST /orders HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\nContent-Length: " +
        std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
    std::string submitResponse = sendHttpRequest(server.port(), post);
    std::string bookResponse = sendHttpRequest(server.port(), "GET /orderbook HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");

    ASSERT_TRUE(submitResponse.find("HTTP/1.1 201 Created") != std::string::npos);
    ASSERT_TRUE(bookResponse.find("\"price\":100") != std::string::npos);
    ASSERT_TRUE(bookResponse.find("\"quantity\":5") != std::string::npos);

    server.stop();
    engine.stop(true);
}

TEST_CASE(TestA27_HttpRejectsMalformedOrder) {
    MatchingEngine engine;
    ApiServer api(engine);
    HttpApiServer server(api, "127.0.0.1", 0);
    engine.start();
    server.start();

    const std::string body = "{\"side\":\"BUY\",\"orderType\":\"LIMIT\",\"price\":100}";
    std::string post = "POST /orders HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\nContent-Length: " +
        std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
    std::string response = sendHttpRequest(server.port(), post);

    ASSERT_TRUE(response.find("HTTP/1.1 400 Bad Request") != std::string::npos);
    ASSERT_TRUE(response.find("quantity is required") != std::string::npos);

    server.stop();
    engine.stop(true);
}

TEST_CASE(TestA28_WebSocketHandshake) {
    MatchingEngine engine;
    ApiServer api(engine);
    HttpApiServer server(api, "127.0.0.1", 0);
    engine.start();
    server.start();

    std::string response = sendHttpRequest(server.port(),
        "GET /ws HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n");

    ASSERT_TRUE(response.find("HTTP/1.1 101 Switching Protocols") != std::string::npos);
    ASSERT_TRUE(response.find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != std::string::npos);

    server.stop();
    engine.stop(true);
}

TEST_CASE(TestA29_WebSocketBroadcasterRunsOutsideMatchingThread) {
    MatchingEngine engine;
    ApiServer api(engine);
    std::atomic<int> delivered{0};
    WebSocketBroadcaster broadcaster(api.outboundQueue());
    broadcaster.addSink([&delivered](const OutboundMessage&) {
        ++delivered;
    });
    broadcaster.start();
    engine.start();

    api.submitOrder(ApiNewOrderRequest{"WS", "AAPL", Side::Buy, OrderType::Limit, 100, 1});
    for (int i = 0; i < 50 && delivered.load() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    ASSERT_TRUE(delivered.load() > 0);
    engine.stop(true);
    broadcaster.stop();
}

int main() {
    std::cout << "=========================================================\n";
    std::cout << "  REST/WebSocket API Adapter Test Suite (Phase 4)\n";
    std::cout << "=========================================================\n";
    std::cout << "Total API Tests Executed: " << g_apiTotalTests << "\n";
    std::cout << "Passed: " << g_apiPassedTests << "\n";
    std::cout << "Failed: " << g_apiFailedTests << "\n";
    std::cout << "=========================================================\n";

    if (g_apiFailedTests == 0) {
        std::cout << "ALL " << g_apiPassedTests << " API ADAPTER TESTS PASSED SUCCESSFULLY!\n";
        return 0;
    }
    std::cerr << "FAILURES DETECTED: " << g_apiFailedTests << " API adapter tests failed!\n";
    return 1;
}
